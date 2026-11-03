/* Port of src/app/bootstrap/server-browser-addresses.ts. Throws are qa_raise
 * ("Error" as the TS `new Error`); a raise releases what was built, so a
 * catching caller (StartupServerBrowser.open's tolerate) leaks nothing.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser_addresses.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/server_master_list.h"
#include "qa/foundation/support.h"

/* UTF-16 code units of UTF-8 text (a 4-byte sequence is a surrogate pair). */
static size_t utf16_length(const char *text) {
  size_t units = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xc0) != 0x80) units += *p >= 0xf0 ? 2 : 1;
  return units;
}
const char *qa_app_direct_server_text(const char *value) {
  if (value == NULL) qa_raise("Error", 0, 0, "Invalid direct server address");
  size_t length = strlen(value), units = utf16_length(value);
  if (units == 0 || units > 255) qa_raise("Error", 0, 0, "Invalid direct server address");
  /* /\s|[\x00-\x1f\x7f]/ */
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c < 0x20 || c == 0x7f || qa_app_js_space_at(value, length, i) != 0) qa_raise("Error", 0, 0, "Invalid direct server address");
  }
  return value;
}
const char *qa_app_direct_server_text_value(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_STRING) qa_raise("Error", 0, 0, "Invalid direct server address");
  return qa_app_direct_server_text(value->text);
}
void qa_app_parse_network_address_value(const qa_save_value *value, qa_net_address *out) {
  qa_net_parse_address_value(value, out);
}

typedef struct read_scope {
  qa_save_value *value;
  qa_app_direct_server_address *servers; size_t count;
} read_scope;
static void read_scope_release(void *arg) {
  read_scope *scope = arg;
  if (scope->value != NULL) qa_save_value_free(scope->value);
  scope->value = NULL;
  qa_app_direct_servers_free(scope->servers, scope->count);
  scope->servers = NULL; scope->count = 0;
}
qa_app_direct_server_address *qa_app_read_direct_servers(const char *text, size_t *out_count) {
  *out_count = 0;
  if (utf16_length(text) > 65536) qa_raise("Error", 0, 0, "Saved direct server list is too large");
  read_scope scope = {NULL, NULL, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, read_scope_release, &scope);
  scope.value = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (scope.value == NULL) qa_range_error("Out of memory");
  const qa_save_value *value = scope.value;
  const qa_save_value *version = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *servers = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "servers") : NULL;
  if (value->kind != QA_SAVE_OBJECT || version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || servers == NULL || servers->kind != QA_SAVE_ARRAY || servers->item_count > QA_APP_MAXIMUM_DIRECT_SERVERS)
    qa_raise("Error", 0, 0, "Invalid direct server list");
  scope.servers = calloc(servers->item_count ? servers->item_count : 1, sizeof *scope.servers);
  if (scope.servers == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < servers->item_count; i++) {
    const qa_save_value *entry = servers->items[i];
    if (entry->kind != QA_SAVE_OBJECT || qa_save_value_get(entry, "remote") == NULL || qa_save_value_get(entry, "address") == NULL)
      qa_raise("Error", 0, 0, "Invalid direct server entry");
    const char *remote = qa_app_direct_server_text_value(qa_save_value_get(entry, "remote"));
    qa_app_direct_server_address *out = &scope.servers[scope.count];
    qa_net_parse_address_value(qa_save_value_get(entry, "address"), &out->address);
    size_t remote_length = strlen(remote);
    out->remote = malloc(remote_length + 1);
    if (out->remote != NULL) memcpy(out->remote, remote, remote_length + 1);
    scope.count++;
    if (out->remote == NULL) qa_range_error("Out of memory");
    if (out->address.kind != QA_NET_IPV4 && out->address.kind != QA_NET_IPV6)
      qa_raise("Error", 0, 0, "Direct server requires an IP address");
  }
  qa_cleanup_pop(&guard);
  qa_save_value_free(scope.value);
  *out_count = scope.count;
  return scope.servers;
}
void qa_app_direct_servers_free(qa_app_direct_server_address *servers, size_t count) {
  if (servers == NULL) return;
  for (size_t i = 0; i < count; i++) { free(servers[i].remote); qa_net_address_destroy(&servers[i].address); }
  free(servers);
}
