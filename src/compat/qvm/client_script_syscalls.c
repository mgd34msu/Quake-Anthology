/* Port of src/compat/qvm/client-script-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_script_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

struct qa_qvm_client_scripts {
  qa_qvm_client_script_services services;
  qa_qvmx_script_reader readers[64];
  bool occupied[64];
  bool pending[64];
  bool closed;
  int operations;
};

qa_qvm_client_scripts *qa_qvm_client_scripts_create(qa_qvm_client_script_services services) {
  qa_qvm_client_scripts *scripts = calloc(1, sizeof *scripts);
  if (scripts == NULL) return NULL;
  scripts->services = services;
  return scripts;
}

void qa_qvm_client_scripts_destroy(qa_qvm_client_scripts *scripts) {
  if (scripts == NULL) return;
  for (int i = 0; i < 64; i++)
    if (scripts->occupied[i]) scripts->readers[i].dispose(scripts->readers[i].ctx);
  free(scripts);
}

qa_qvm_client_script_services qa_qvm_client_scripts_get_services(
    const qa_qvm_client_scripts *scripts) {
  return scripts->services;
}

void qa_qvm_client_scripts_assert_current(qa_qvm_client_scripts *scripts) {
  if (scripts->closed) qa_range_error("QVM client scripts have been closed");
  scripts->services.assert_current(scripts->services.ctx);
}

static void completed(qa_qvm_client_scripts *scripts) {
  scripts->operations--;
  if (scripts->closed && scripts->operations == 0)
    scripts->services.memory->dispose(scripts->services.memory->ctx);
}

typedef struct report_ctx {
  qa_qvm_client_scripts *scripts;
} report_ctx;

static void report_diagnostic(void *ctx, qa_qvmx_script_diagnostic diagnostic) {
  report_ctx *report = ctx;
  qa_qvm_client_scripts_assert_current(report->scripts);
  char *message = NULL;
  int length = snprintf(NULL, 0, "file %s, line %d: %s\n", diagnostic.path, (int)diagnostic.line,
                        diagnostic.message);
  message = malloc((size_t)length + 1);
  if (message == NULL) qa_range_error("out of memory");
  snprintf(message, (size_t)length + 1, "file %s, line %d: %s\n", diagnostic.path,
           (int)diagnostic.line, diagnostic.message);
  report->scripts->services.print(report->scripts->services.ctx, message);
  free(message);
  qa_qvm_client_scripts_assert_current(report->scripts);
}

static bool load_source(qa_qvm_client_scripts *scripts, const char *filename,
                        qa_qvmx_script_source *out) {
  qa_qvm_client_scripts_assert_current(scripts);
  size_t length = strlen(filename);
  if (length >= 32000)
    qa_range_error("Com_sprintf formatted bot path exceeds its 32000-byte source buffer");
  if (length >= 64) {
    char message[96];
    snprintf(message, sizeof message, "Com_sprintf: overflow of %zu in 64\n", length);
    scripts->services.print(scripts->services.ctx, message);
    qa_qvm_client_scripts_assert_current(scripts);
  }
  char truncated[64];
  size_t kept = length < 63 ? length : 63;
  memcpy(truncated, filename, kept);
  truncated[kept] = '\0';
  qa_content_opened_resource resource;
  bool found = qa_content_mounted_open(scripts->services.mounts, truncated, NULL, NULL, &resource);
  qa_qvm_client_scripts_assert_current(scripts);
  if (!found) return false;
  qa_qvmx_script_source source = scripts->services.factory->allocate_source(
      scripts->services.factory->ctx, resource.byte_length, filename, scripts->services.memory);
  memcpy(source.buffer, resource.bytes, resource.byte_length);
  free(resource.bytes);
  source.compress(source.ctx);
  *out = source;
  return true;
}

typedef struct include_ctx {
  qa_qvm_client_scripts *scripts;
} include_ctx;

static bool resolve_include(void *ctx, qa_qvmx_include_request request,
                            qa_qvmx_script_source *out) {
  include_ctx *include = ctx;
  qa_qvm_client_scripts *scripts = include->scripts;
  bool system = strcmp(request.kind, "system") == 0;
  if (system && strlen(request.requested_path) >= 64)
    qa_range_error("PC include system filename exceeds 64 source bytes");
  /* Collapse runs of backslash/slash to one slash, as the TS regex. */
  char *filename = malloc(strlen(request.requested_path) + 1);
  if (filename == NULL) qa_range_error("out of memory");
  size_t used = 0;
  for (size_t i = 0; request.requested_path[i] != '\0'; i++) {
    char c = request.requested_path[i];
    if (c == '\\' || c == '/') {
      if (used > 0 && filename[used - 1] == '/') continue;
      filename[used++] = '/';
    } else {
      filename[used++] = c;
    }
  }
  filename[used] = '\0';
  bool found = load_source(scripts, filename, out);
  if (found || system) {
    free(filename);
    return found;
  }
  const char *prefix = request.include_path == NULL ? "" : request.include_path;
  size_t retry_length = strlen(prefix) + strlen(filename);
  if (retry_length >= 64) {
    free(filename);
    qa_range_error("PC quoted include retry exceeds 64 source bytes");
  }
  char *retry = malloc(retry_length + 1);
  if (retry == NULL) qa_range_error("out of memory");
  strcpy(retry, prefix);
  strcat(retry, filename);
  free(filename);
  found = load_source(scripts, retry, out);
  free(retry);
  return found;
}

static qa_qvmx_script_reader_options reader_options(qa_qvm_client_scripts *scripts,
                                                    report_ctx *report) {
  qa_qvmx_script_reader_options options;
  options.memory = scripts->services.memory;
  options.globals = scripts->services.globals;
  options.report_ctx = report;
  options.report = report_diagnostic;
  return options;
}

qa_save_value *qa_qvm_client_scripts_capture(qa_qvm_client_scripts *scripts) {
  qa_qvm_client_scripts_assert_current(scripts);
  bool any_pending = false;
  for (int i = 0; i < 64; i++) any_pending = any_pending || scripts->pending[i];
  if (scripts->operations != 0 || any_pending)
    qa_range_error("Cannot checkpoint an active client script operation");
  qa_save_value *image = NULL;
  qa_qvmx_bot_memory_capture capture;
  if (!scripts->services.memory->checkpoint(scripts->services.memory->ctx, &image, &capture))
    return NULL;
  qa_save_value *globals = scripts->services.globals->capture_owned(scripts->services.globals->ctx);
  qa_save_value *readers = qa_save_value_array();
  qa_save_value *object = qa_save_value_object();
  if (globals == NULL || readers == NULL || object == NULL) {
    qa_save_value_free(image);
    qa_save_value_free(globals);
    qa_save_value_free(readers);
    qa_save_value_free(object);
    return NULL;
  }
  for (int handle = 0; handle < 64; handle++) {
    if (!scripts->occupied[handle]) continue;
    qa_save_value *row = qa_save_value_object();
    qa_save_value *handle_value = qa_save_value_number(handle);
    qa_save_value *state = scripts->readers[handle].capture(scripts->readers[handle].ctx, capture);
    if (row == NULL || handle_value == NULL || state == NULL ||
        !qa_save_value_set(row, "handle", handle_value) || !qa_save_value_set(row, "state", state) ||
        !qa_save_value_push(readers, row)) {
      qa_save_value_free(row);
      qa_save_value_free(handle_value);
      qa_save_value_free(state);
      qa_save_value_free(readers);
      qa_save_value_free(globals);
      qa_save_value_free(image);
      qa_save_value_free(object);
      return NULL;
    }
  }
  if (!qa_save_value_set(object, "memory", image) || !qa_save_value_set(object, "globals", globals) ||
      !qa_save_value_set(object, "readers", readers)) {
    qa_save_value_free(object);
    qa_save_value_free(image);
    qa_save_value_free(globals);
    qa_save_value_free(readers);
    return NULL;
  }
  return object;
}

static void reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

void qa_qvm_client_scripts_restore(qa_qvm_client_scripts *scripts, const qa_save_value *value) {
  qa_qvm_client_scripts_assert_current(scripts);
  bool any = false, any_pending = false;
  for (int i = 0; i < 64; i++) {
    any = any || scripts->occupied[i];
    any_pending = any_pending || scripts->pending[i];
  }
  if (any || scripts->operations != 0 || any_pending)
    qa_range_error("Client script restore requires an empty idle owner");
  qa_save_reader reader, memory_reader, globals_reader, readers_reader;
  if (!qa_save_reader_init(&reader, value, "client-scripts")) qa_range_error("out of memory");
  reader_field(&reader, "memory", &memory_reader);
  qa_qvmx_bot_memory_restore memory =
      scripts->services.memory->restore(scripts->services.memory->ctx, memory_reader.value);
  qa_save_reader_destroy(&memory_reader);
  reader_field(&reader, "globals", &globals_reader);
  scripts->services.globals->restore_owned(scripts->services.globals->ctx, globals_reader.value);
  qa_save_reader_destroy(&globals_reader);
  reader_field(&reader, "readers", &readers_reader);
  size_t count = qa_save_reader_list_count(&readers_reader);
  include_ctx include = {scripts};
  report_ctx report = {scripts};
  qa_qvmx_script_resolver resolver = {&include, resolve_include};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, handle_reader, state_reader;
    if (!qa_save_reader_item(&readers_reader, i, &item)) qa_range_error("out of memory");
    reader_field(&item, "handle", &handle_reader);
    double handle = qa_save_reader_integer_min(&handle_reader, 1);
    qa_save_reader_destroy(&handle_reader);
    if (handle >= 64 || scripts->occupied[(size_t)handle])
      qa_save_reader_fail(&reader, "invalid client script slot");
    reader_field(&item, "state", &state_reader);
    qa_qvmx_script_reader restored = scripts->services.factory->restore(
        scripts->services.factory->ctx, state_reader.value, resolver,
        reader_options(scripts, &report), memory);
    qa_save_reader_destroy(&state_reader);
    scripts->readers[(size_t)handle] = restored;
    scripts->occupied[(size_t)handle] = true;
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&readers_reader);
  qa_save_reader_destroy(&reader);
}

void qa_qvm_client_scripts_close_all(qa_qvm_client_scripts *scripts) {
  if (scripts->closed) return;
  scripts->closed = true;
  for (int i = 0; i < 64; i++) {
    if (scripts->occupied[i]) scripts->readers[i].dispose(scripts->readers[i].ctx);
    scripts->occupied[i] = false;
    scripts->pending[i] = false;
  }
  if (scripts->operations == 0) scripts->services.memory->dispose(scripts->services.memory->ctx);
}

double qa_qvm_client_scripts_add_define(qa_qvm_client_scripts *scripts, const char *text) {
  qa_qvm_client_scripts_assert_current(scripts);
  return scripts->services.globals->add(scripts->services.globals->ctx, text) ? 1 : 0;
}

double qa_qvm_client_scripts_load(qa_qvm_client_scripts *scripts, const char *filename) {
  qa_qvm_client_scripts_assert_current(scripts);
  int handle = 1;
  while (handle < 64 && (scripts->occupied[handle] || scripts->pending[handle])) handle++;
  if (handle == 64) return 0;
  scripts->pending[handle] = true;
  scripts->operations++;
  qa_qvmx_script_source root;
  bool found = load_source(scripts, filename, &root);
  if (!found) {
    scripts->pending[handle] = false;
    completed(scripts);
    return 0;
  }
  /* TS disposes the root when assertCurrent fails after loading. */
  qa_qvm_client_scripts_assert_current(scripts);
  include_ctx include = {scripts};
  report_ctx report = {scripts};
  qa_qvmx_script_resolver resolver = {&include, resolve_include};
  qa_qvmx_script_reader reader = scripts->services.factory->open(
      scripts->services.factory->ctx, root, resolver, reader_options(scripts, &report));
  scripts->readers[handle] = reader;
  scripts->occupied[handle] = true;
  scripts->pending[handle] = false;
  completed(scripts);
  return handle;
}

double qa_qvm_client_scripts_free(qa_qvm_client_scripts *scripts, double handle) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (handle < 0 || handle >= 64 || !scripts->occupied[(size_t)handle]) return 0;
  scripts->readers[(size_t)handle].dispose(scripts->readers[(size_t)handle].ctx);
  scripts->occupied[(size_t)handle] = false;
  return 1;
}

static void validate_current(void *ctx) { qa_qvm_client_scripts_assert_current(ctx); }

double qa_qvm_client_scripts_read(qa_qvm_client_scripts *scripts, double handle,
                                  qa_qvm_script_publish publish) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (handle < 0 || handle >= 64 || !scripts->occupied[(size_t)handle]) return 0;
  qa_qvmx_script_reader *reader = &scripts->readers[(size_t)handle];
  scripts->operations++;
  int next = reader->next(reader->ctx, validate_current, scripts);
  bool read = next > 0;
  qa_qvm_client_scripts_assert_current(scripts);
  if (!scripts->occupied[(size_t)handle] || &scripts->readers[(size_t)handle] != reader) {
    completed(scripts);
    qa_range_error("QVM source handle was freed during token read");
  }
  publish.call(publish.ctx, reader);
  completed(scripts);
  return read ? 1 : 0;
}

bool qa_qvm_client_scripts_position(qa_qvm_client_scripts *scripts, double handle,
                                    qa_qvmx_script_position *out) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (handle < 0 || handle >= 64 || !scripts->occupied[(size_t)handle]) return false;
  qa_qvmx_script_reader *reader = &scripts->readers[(size_t)handle];
  return reader->get_position(reader->ctx, out);
}

typedef struct token_publish_ctx {
  const qa_qvm_host_call *call;
  int32_t pointer;
} token_publish_ctx;

static void publish_token(void *ctx, qa_qvmx_script_reader *reader) {
  token_publish_ctx *publish = ctx;
  qa_qvmy_script_token_record token = reader->raw_token(reader->ctx);
  qa_qvm_writable_view view;
  qa_qvm_memory_view(publish->call->guest, publish->pointer, QA_QVM_SCRIPT_TOKEN_BYTES, 0, &view);
  qa_qvm_write_script_token(&view, &token);
}

bool qa_qvm_client_script_syscall(const qa_qvm_host_call *call, qa_qvm_client_scripts *scripts,
                                  qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") == 0) return false;
  bool ui = strcmp(call->role, "ui") == 0;
  int32_t define = ui ? QA_QVM_UI_IMPORT_UI_PC_ADD_GLOBAL_DEFINE
                      : QA_QVM_CGAME_IMPORT_CG_PC_ADD_GLOBAL_DEFINE;
  int32_t load = ui ? QA_QVM_UI_IMPORT_UI_PC_LOAD_SOURCE : QA_QVM_CGAME_IMPORT_CG_PC_LOAD_SOURCE;
  if (call->code == define) {
    char *text = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 1));
    if (text == NULL) qa_range_error("out of memory");
    double result = qa_qvm_client_scripts_add_define(scripts, text);
    free(text);
    *out = qa_int32(result);
    return true;
  }
  switch (call->code - load) {
    case 0: {
      char *filename = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 1));
      if (filename == NULL) qa_range_error("out of memory");
      double result = qa_qvm_client_scripts_load(scripts, filename);
      free(filename);
      *out = qa_int32(result);
      return true;
    }
    case 1:
      *out = qa_int32(qa_qvm_client_scripts_free(scripts, qa_qvm_host_call_word(call, 1)));
      return true;
    case 2: {
      int32_t handle = qa_qvm_host_call_word(call, 1);
      token_publish_ctx pctx = {call, qa_qvm_host_call_word(call, 2)};
      qa_qvm_script_publish publish = {&pctx, publish_token};
      *out = qa_int32(qa_qvm_client_scripts_read(scripts, handle, publish));
      return true;
    }
    case 3: {
      qa_qvmx_script_position position;
      if (!qa_qvm_client_scripts_position(scripts, qa_qvm_host_call_word(call, 1), &position)) {
        *out = 0;
        return true;
      }
      char filename[65];
      size_t length = strlen(position.filename);
      if (length > 64) length = 64;
      memcpy(filename, position.filename, length);
      filename[length] = '\0';
      qa_qvm_memory_write_bounded_string(call->guest, qa_qvm_host_call_word(call, 2), filename,
                                         (int64_t)length + 1);
      qa_qvm_writable_view view;
      qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32(position.line), true);
      *out = 1;
      return true;
    }
    default: return false;
  }
}
