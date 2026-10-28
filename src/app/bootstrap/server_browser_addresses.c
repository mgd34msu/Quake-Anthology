/* Port of src/app/bootstrap/server-browser-addresses.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser_addresses.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
const char *qa_app_direct_server_text(const char *value) {
  if (value == NULL) qa_range_error("Invalid direct server address");
  size_t len = strlen(value);
  if (len == 0 || len > 255) qa_range_error("Invalid direct server address");
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c < 0x20 || c == 0x7f)
      qa_range_error("Invalid direct server address");
  }
  return value;
}
const char *qa_app_direct_server_text_value(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_STRING) qa_range_error("Invalid direct server address");
  return qa_app_direct_server_text(value->text);
}
void qa_app_parse_network_address_value(const qa_save_value *value, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  if (value == NULL || value->kind != QA_SAVE_OBJECT) qa_range_error("Invalid network address record");
  const qa_save_value *kind = qa_save_value_get(value, "kind");
  if (kind == NULL) qa_range_error("Invalid network address record");
  const char *kind_text = kind->kind == QA_SAVE_STRING ? kind->text : "";
  if (strcmp(kind_text, "loopback") == 0) {
    const qa_save_value *id = qa_save_value_get(value, "id");
    if (id && id->kind == QA_SAVE_STRING && id->text[0] != '\0') {
      if (!qa_net_parse_address(out, "loopback", id->text, NULL, 0, 0, false, 0, false)) qa_range_error("Out of memory");
      return;
    }
  }
  const qa_save_value *port = qa_save_value_get(value, "port");
  if (port == NULL || port->kind != QA_SAVE_NUMBER) qa_range_error("Network address has no port");
  if (strcmp(kind_text, "ipv6") == 0) {
    const qa_save_value *host = qa_save_value_get(value, "host");
    if (host && host->kind == QA_SAVE_STRING) {
      if (!qa_net_parse_address(out, "ipv6", host->text, NULL, 0, 0, false, port->number, true)) qa_range_error("Out of memory");
      return;
    }
  }
  if (strcmp(kind_text, "ipv4") == 0) {
    const qa_save_value *host = qa_save_value_get(value, "host");
    if (host && host->kind == QA_SAVE_ARRAY && host->item_count == 4) {
      double nums[4]; bool ok = true;
      for (int i = 0; i < 4; i++) { if (host->items[i]->kind != QA_SAVE_NUMBER) ok = false; else nums[i] = host->items[i]->number; }
      if (ok) {
        if (!qa_net_parse_address(out, "ipv4", NULL, nums, 4, 0, false, port->number, true)) qa_range_error("Out of memory");
        return;
      }
    }
  }
  if (strcmp(kind_text, "ipx") == 0) {
    const qa_save_value *node = qa_save_value_get(value, "node");
    const qa_save_value *network = qa_save_value_get(value, "network");
    if (node && node->kind == QA_SAVE_ARRAY && node->item_count == 6 && network && network->kind == QA_SAVE_NUMBER) {
      double nums[6]; bool ok = true;
      for (int i = 0; i < 6; i++) { if (node->items[i]->kind != QA_SAVE_NUMBER) ok = false; else nums[i] = node->items[i]->number; }
      if (ok) {
        if (!qa_net_parse_address(out, "ipx", NULL, nums, 6, (int64_t)network->number, true, port->number, true)) qa_range_error("Out of memory");
        return;
      }
    }
  }
  qa_range_error("Invalid network address fields");
}
qa_app_direct_server_address *qa_app_read_direct_servers(const char *text, size_t *out_count) {
  if (strlen(text) > 65536) qa_range_error("Saved direct server list is too large");
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (value == NULL) qa_range_error("Out of memory");
  const qa_save_value *version = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *servers = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, "servers") : NULL;
  if (value->kind != QA_SAVE_OBJECT || version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || servers == NULL || servers->kind != QA_SAVE_ARRAY || servers->item_count > QA_APP_MAXIMUM_DIRECT_SERVERS)
    qa_range_error("Invalid direct server list");
  size_t count = servers->item_count;
  qa_app_direct_server_address *out = calloc(count ? count : 1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *entry = servers->items[i];
    if (entry->kind != QA_SAVE_OBJECT || qa_save_value_get(entry, "remote") == NULL || qa_save_value_get(entry, "address") == NULL)
      qa_range_error("Invalid direct server entry");
    const char *remote = qa_app_direct_server_text_value(qa_save_value_get(entry, "remote"));
    out[i].remote = qa_input_dup(remote);
    if (!out[i].remote) qa_range_error("Out of memory");
    qa_app_parse_network_address_value(qa_save_value_get(entry, "address"), &out[i].address);
    if (out[i].address.kind != QA_NET_IPV4 && out[i].address.kind != QA_NET_IPV6)
      qa_range_error("Direct server requires an IP address");
  }
  qa_save_value_free(value);
  *out_count = count;
  return out;
}
void qa_app_direct_servers_free(qa_app_direct_server_address *servers, size_t count) {
  for (size_t i = 0; i < count; i++) { free(servers[i].remote); qa_net_address_destroy(&servers[i].address); }
  free(servers);
}
