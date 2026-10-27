/* Port of src/compat/qvm/client-browser-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_browser_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static bool has_server_record(double source, double index) {
  return index >= 0 && (source == 2 ? index < 4096
                                    : (source == 0 || source == 1 || source == 3) && index < 128);
}

typedef struct write_ctx {
  qa_qvm_memory *memory;
  int32_t pointer, capacity;
} write_ctx;

static void write_string(void *ctx, const char *text) {
  write_ctx *write = ctx;
  qa_qvm_memory_write_string(write->memory, write->pointer, text, write->capacity);
}

static void clear_byte(qa_qvm_memory *memory, int32_t pointer) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory, pointer, 1, 0, &view);
  qa_qvm_writable_view_set_uint8(&view, 0, 0);
}

typedef struct name_ctx {
  qa_qvm_memory *memory;
  int32_t word;
} name_ctx;

static char *server_name(void *ctx) {
  name_ctx *source = ctx;
  qa_qvm_memory_span span;
  if (!qa_qvm_memory_pointer(source->memory, source->word, &span))
    qa_range_error("Q_strncpyz: NULL src");
  char *name = malloc(32);
  if (name == NULL) qa_range_error("out of memory");
  size_t used = 0;
  for (int index = 0; index < 31; index++) {
    if (index >= span.byte_length) {
      free(name);
      qa_range_error("Server name copy exceeds QVM allocation");
    }
    uint8_t byte = source->memory->bytes[span.byte_offset + index];
    if (byte == 0) break;
    name[used++] = (char)byte;
  }
  name[used] = '\0';
  return name;
}

static char *read_guest_string(void *ctx) {
  name_ctx *source = ctx;
  char *text = qa_qvm_memory_read_string(source->memory, source->word);
  if (text == NULL) qa_range_error("out of memory");
  return text;
}

bool qa_qvm_client_browser_syscall(const qa_qvm_host_call *call, qa_qvmx_browser_view browser,
                                   qa_qvm_host_result *out) {
  if (strcmp(call->role, "ui") != 0) return false;
  qa_qvm_memory *memory = call->guest;
  if (strcmp(call->kind, "extension") == 0 && call->has_abi_profile &&
      strcmp(call->abi_profile, "q3-1.16n-base") == 0 && call->code >= 46 && call->code <= 49) {
    double source = call->code <= 47 ? 0 : 2;
    if (call->code == 46 || call->code == 48) {
      *out = qa_int32(browser.get_server_count(browser.ctx, source));
      return true;
    }
    int32_t index = qa_qvm_host_call_word(call, 1);
    int32_t pointer = qa_qvm_host_call_word(call, 2);
    int32_t capacity = qa_qvm_host_call_word(call, 3);
    if (capacity != 0) clear_byte(memory, pointer);
    write_ctx write = {memory, pointer, capacity};
    qa_qvm_browser_write writer = {&write, write_string};
    browser.get_server_address_string(browser.ctx, source, index, capacity, writer);
    *out = 0;
    return true;
  }
  if (strcmp(call->kind, "engine") != 0) return false;
  int32_t trap = call->code;
  switch (trap) {
    case 46: *out = qa_int32(browser.get_ping_queue_count(browser.ctx)); return true;
    case 47: browser.clear_ping(browser.ctx, qa_qvm_host_call_word(call, 1)); *out = 0; return true;
    case 48: {
      int32_t index = qa_qvm_host_call_word(call, 1);
      int32_t destination = qa_qvm_host_call_word(call, 2);
      int32_t capacity = qa_qvm_host_call_word(call, 3);
      int32_t time_pointer = qa_qvm_host_call_word(call, 4);
      write_ctx write = {memory, destination, capacity};
      /* get_ping's write handles the null address in the adapter:
       * the mirror returns the address and the dispatcher writes it
       * here exactly as the TS callback does. */
      qa_qvm_browser_write writer = {&write, write_string};
      qa_qvmx_browser_ping ping = browser.get_ping(browser.ctx, index, capacity, writer);
      if (ping.address == NULL) clear_byte(memory, destination);
      qa_qvm_writable_view view;
      qa_qvm_memory_view(memory, time_pointer, 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32(ping.time), true);
      *out = 0;
      return true;
    }
    case 49: {
      int32_t index = qa_qvm_host_call_word(call, 1);
      int32_t destination = qa_qvm_host_call_word(call, 2);
      int32_t capacity = qa_qvm_host_call_word(call, 3);
      write_ctx write = {memory, destination, capacity};
      qa_qvm_browser_write writer = {&write, write_string};
      const char *info = browser.source_ping_info(browser.ctx, index, capacity, writer);
      if (info == NULL && capacity != 0) clear_byte(memory, destination);
      *out = 0;
      return true;
    }
    case 65:
      *out = qa_int32(browser.get_server_count(browser.ctx, qa_qvm_host_call_word(call, 1)));
      return true;
    case 66:
    case 67: {
      int32_t source = qa_qvm_host_call_word(call, 1);
      int32_t index = qa_qvm_host_call_word(call, 2);
      int32_t destination = qa_qvm_host_call_word(call, 3);
      int32_t capacity = qa_qvm_host_call_word(call, 4);
      if (trap == 67 && destination == 0) {
        *out = 0;
        return true;
      }
      if (trap == 67 || !has_server_record(source, index)) clear_byte(memory, destination);
      if (!has_server_record(source, index)) {
        *out = 0;
        return true;
      }
      write_ctx write = {memory, destination, capacity};
      qa_qvm_browser_write writer = {&write, write_string};
      if (trap == 66) browser.get_server_address_string(browser.ctx, source, index, capacity, writer);
      else browser.get_server_info(browser.ctx, source, index, capacity, writer);
      *out = 0;
      return true;
    }
    case 68: {
      browser.mark_server_visible_value(browser.ctx, qa_qvm_host_call_word(call, 1),
                                        qa_qvm_host_call_word(call, 2),
                                        qa_qvm_host_call_word(call, 3));
      *out = 0;
      return true;
    }
    case 69:
      *out = browser.update_visible_pings(browser.ctx, qa_qvm_host_call_word(call, 1)) ? 1 : 0;
      return true;
    case 70: browser.reset_pings(browser.ctx, qa_qvm_host_call_word(call, 1)); *out = 0; return true;
    case 71: browser.load_cached_servers(browser.ctx); *out = 0; return true;
    case 72: browser.save_servers_to_cache(browser.ctx); *out = 0; return true;
    case 73: {
      int32_t source = qa_qvm_host_call_word(call, 1);
      name_ctx name = {memory, qa_qvm_host_call_word(call, 2)};
      name_ctx address = {memory, qa_qvm_host_call_word(call, 3)};
      qa_qvm_browser_string_source name_source = {&name, server_name};
      qa_qvm_browser_string_source address_source = {&address, read_guest_string};
      *out = qa_int32(browser.add_server(browser.ctx, source, name_source, address_source));
      return true;
    }
    case 74: {
      int32_t source = qa_qvm_host_call_word(call, 1);
      name_ctx address = {memory, qa_qvm_host_call_word(call, 2)};
      qa_qvm_browser_string_source address_source = {&address, read_guest_string};
      browser.remove_server(browser.ctx, source, address_source);
      *out = 0;
      return true;
    }
    case 82: {
      int32_t address = qa_qvm_host_call_word(call, 1);
      int32_t destination = qa_qvm_host_call_word(call, 2);
      int32_t capacity = qa_qvm_host_call_word(call, 3);
      char *text = NULL;
      if (address != 0) {
        text = qa_qvm_memory_read_string(memory, address);
        if (text == NULL) qa_range_error("out of memory");
      }
      write_ctx write = {memory, destination, capacity};
      qa_qvm_browser_write writer = {&write, write_string};
      const char *status =
          browser.server_status(browser.ctx, text, destination != 0, capacity, writer);
      free(text);
      *out = status != NULL ? 1 : 0;
      return true;
    }
    case 83:
      *out = qa_int32(browser.get_server_ping(browser.ctx, qa_qvm_host_call_word(call, 1),
                                              qa_qvm_host_call_word(call, 2)));
      return true;
    case 84:
      *out = qa_int32(browser.server_visibility_value(browser.ctx, qa_qvm_host_call_word(call, 1),
                                                      qa_qvm_host_call_word(call, 2)));
      return true;
    case 85:
      *out = qa_int32(browser.compare_servers(browser.ctx, qa_qvm_host_call_word(call, 1),
                                              qa_qvm_host_call_word(call, 2),
                                              qa_qvm_host_call_word(call, 3),
                                              qa_qvm_host_call_word(call, 4),
                                              qa_qvm_host_call_word(call, 5)));
      return true;
    default: return false;
  }
}
