/* Port of src/compat/qvm/client-script-syscalls.ts. BotMemory
 * checkpoint/restore is qa_bot_memory_checkpoint/restore (TS owner
 * bots/behavior/library/memory.ts); read() catches source failures
 * via qa_ui_script_source_reader_catch. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_script_syscalls.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/bots/behavior/library/memory.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/script_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

/* ---- BotMemory as ScriptMemory, with checkpoint/restore ---- */

typedef struct mem_entry {
  uint8_t *bytes;
  size_t length;
  qa_bot_memory_allocation bot;
  bool live;
} mem_entry;

struct qa_qvm_client_scripts {
  qa_qvm_client_script_services services;
  qa_ui_script_source_reader *readers[64];
  bool occupied[64];
  bool pending[64];
  uint64_t order[64]; /* TS Map insertion order, reported by capture */
  uint64_t next_order;
  char *include_path; /* canonical path of the last resolved include */
  bool closed;
  int operations;
  qa_bot_memory bot_memory;
  qa_ui_script_memory script_memory;
  mem_entry *entries;
  size_t entry_count;
  size_t entry_capacity;
};

static bool track(qa_qvm_client_scripts *scripts, qa_bot_memory_allocation bot) {
  if (scripts->entry_count == scripts->entry_capacity) {
    size_t capacity = scripts->entry_capacity == 0 ? 16 : scripts->entry_capacity * 2;
    mem_entry *entries = realloc(scripts->entries, capacity * sizeof *entries);
    if (entries == NULL) return false;
    scripts->entries = entries;
    scripts->entry_capacity = capacity;
  }
  mem_entry *entry = &scripts->entries[scripts->entry_count++];
  entry->bytes = bot.bytes;
  entry->length = bot.length;
  entry->bot = bot;
  entry->live = true;
  return true;
}

static bool mem_allocate(void *ctx, size_t size, bool clear,
                         qa_ui_script_allocation *out) {
  qa_qvm_client_scripts *scripts = ctx;
  qa_bot_memory_allocation bot;
  if (!qa_bot_memory_allocate(&scripts->bot_memory, (int64_t)size,
                              QA_BOT_MEMORY_HEAP, clear, &bot))
    return false;
  if (!track(scripts, bot)) {
    qa_bot_memory_free(&scripts->bot_memory, &bot);
    return false;
  }
  out->bytes = bot.bytes;
  out->length = bot.length;
  out->record = bot.record;
  return true;
}

static void mem_free(void *ctx, qa_ui_script_allocation *allocation) {
  qa_qvm_client_scripts *scripts = ctx;
  for (size_t i = scripts->entry_count; i-- > 0;) {
    mem_entry *entry = &scripts->entries[i];
    if (entry->live && entry->bytes == allocation->bytes) {
      qa_bot_memory_free(&scripts->bot_memory, &entry->bot);
      entry->live = false;
      return;
    }
  }
  /* Already released by a dispose: C teardown only (TS relies on GC
   * after reader.dispose()); normal engine frees always find a live
   * entry above. */
}

static void mem_dispose(qa_qvm_client_scripts *scripts) {
  qa_bot_memory_dispose(&scripts->bot_memory);
  for (size_t i = 0; i < scripts->entry_count; i++) scripts->entries[i].live = false;
}

/* BotMemory checkpoint/restore (qa_bot_memory) behind the
 * ScriptMemory capture/restore fn+ctx shapes. */
static int64_t capture_reference(void *ctx, qa_ui_script_allocation *allocation) {
  qa_bot_memory_allocation bot = {allocation->bytes, allocation->length,
                                   allocation->record};
  return qa_bot_memory_capture_reference(ctx, &bot);
}

static qa_ui_script_allocation restored_allocation(void *ctx, int64_t id) {
  qa_bot_memory_allocation bot = qa_bot_memory_restored_allocation(ctx, id);
  qa_ui_script_allocation out = {bot.bytes, bot.length, bot.record};
  return out;
}

static void reader_field(const qa_save_reader *reader, const char *name,
                         qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

/* ---- QvmClientScripts ---- */

qa_qvm_client_scripts *qa_qvm_client_scripts_create(qa_qvm_client_script_services services) {
  qa_qvm_client_scripts *scripts = calloc(1, sizeof *scripts);
  if (scripts == NULL) return NULL;
  scripts->services = services;
  qa_bot_memory_init(&scripts->bot_memory);
  scripts->script_memory.ctx = scripts;
  scripts->script_memory.allocate = mem_allocate;
  scripts->script_memory.free_allocation = mem_free;
  return scripts;
}

void qa_qvm_client_scripts_destroy(qa_qvm_client_scripts *scripts) {
  if (scripts == NULL) return;
  for (int i = 0; i < 64; i++)
    if (scripts->occupied[i]) qa_ui_script_source_reader_destroy(scripts->readers[i]);
  mem_dispose(scripts);
  qa_bot_memory_destroy(&scripts->bot_memory);
  free(scripts->include_path);
  free(scripts->entries);
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
  if (scripts->closed && scripts->operations == 0) mem_dispose(scripts);
}

static void report_diagnostic(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  qa_qvm_client_scripts *scripts = ctx;
  qa_qvm_client_scripts_assert_current(scripts);
  int length = snprintf(NULL, 0, "file %s, line %lld: %s\n",
                        diagnostic->location.path,
                        (long long)diagnostic->location.line, diagnostic->message);
  char *message = malloc((size_t)length + 1);
  if (message == NULL) qa_range_error("out of memory");
  snprintf(message, (size_t)length + 1, "file %s, line %lld: %s\n",
           diagnostic->location.path, (long long)diagnostic->location.line,
           diagnostic->message);
  scripts->services.print(scripts->services.ctx, message);
  free(message);
  qa_qvm_client_scripts_assert_current(scripts);
}

static qa_ui_source_script_storage *load_source(qa_qvm_client_scripts *scripts,
                                                const char *filename) {
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
  if (!found) return NULL;
  qa_ui_source_script_storage *source = qa_ui_script_allocate_source(
      (int64_t)resource.byte_length, filename, &scripts->script_memory);
  if (source == NULL) qa_range_error("out of memory");
  size_t buffer_length = 0;
  uint8_t *buffer = qa_ui_source_script_buffer(source, &buffer_length);
  memcpy(buffer, resource.bytes, resource.byte_length);
  free(resource.bytes);
  qa_ui_source_script_compress(source);
  return source;
}

static bool resolve_include(void *ctx, const qa_ui_script_include_request *request,
                            qa_ui_script_source *out) {
  qa_qvm_client_scripts *scripts = ctx;
  bool system = request->kind == QA_UI_SCRIPT_INCLUDE_SYSTEM;
  if (system && strlen(request->requested_path) >= 64)
    qa_range_error("PC include system filename exceeds 64 source bytes");
  /* Collapse runs of backslash/slash to one slash, as the TS regex. */
  char *filename = malloc(strlen(request->requested_path) + 1);
  if (filename == NULL) qa_range_error("out of memory");
  size_t used = 0;
  for (size_t i = 0; request->requested_path[i] != '\0'; i++) {
    char c = request->requested_path[i];
    if (c == '\\' || c == '/') {
      if (used > 0 && filename[used - 1] == '/') continue;
      filename[used++] = '/';
    } else {
      filename[used++] = c;
    }
  }
  filename[used] = '\0';
  qa_ui_source_script_storage *storage = load_source(scripts, filename);
  /* A TS ScriptSource carries the path it was allocated with: the
   * normalized name or the retry, never the raw requested spelling. */
  char *canonical = filename;
  if (storage == NULL && !system) {
    const char *prefix = request->include_path == NULL ? "" : request->include_path;
    size_t retry_length = strlen(prefix) + strlen(filename);
    if (retry_length >= 64) {
      free(filename);
      qa_range_error("PC quoted include retry exceeds 64 source bytes");
    }
    char *retry = malloc(retry_length + 1);
    if (retry == NULL) qa_range_error("out of memory");
    strcpy(retry, prefix);
    strcat(retry, filename);
    storage = load_source(scripts, retry);
    free(filename);
    canonical = retry;
  }
  free(scripts->include_path);
  scripts->include_path = canonical;
  if (storage == NULL) return false;
  out->path = canonical;
  out->text = NULL;
  out->text_length = 0;
  out->storage = storage;
  return true;
}

static qa_ui_script_preprocessor_options reader_options(qa_qvm_client_scripts *scripts) {
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.memory = &scripts->script_memory;
  options.globals = scripts->services.globals;
  options.report = report_diagnostic;
  options.report_ctx = scripts;
  return options;
}

qa_save_value *qa_qvm_client_scripts_capture(qa_qvm_client_scripts *scripts) {
  qa_qvm_client_scripts_assert_current(scripts);
  bool any_pending = false;
  for (int i = 0; i < 64; i++) any_pending = any_pending || scripts->pending[i];
  if (scripts->operations != 0 || any_pending)
    qa_range_error("Cannot checkpoint an active client script operation");
  qa_bot_memory_capture bot_capture;
  if (!qa_bot_memory_checkpoint(&scripts->bot_memory, &bot_capture)) return NULL;
  qa_save_value *image = bot_capture.image;
  bot_capture.image = NULL;
  qa_ui_script_memory_capture capture = {&bot_capture, capture_reference};
  qa_save_value *globals =
      qa_ui_script_global_defines_capture_owned_save_state(scripts->services.globals);
  qa_save_value *readers = qa_save_value_array();
  qa_save_value *object = qa_save_value_object();
  if (image == NULL || globals == NULL || readers == NULL || object == NULL) {
    qa_save_value_free(image);
    qa_save_value_free(globals);
    qa_save_value_free(readers);
    qa_save_value_free(object);
    qa_bot_memory_capture_destroy(&bot_capture);
    return NULL;
  }
  /* TS [...readers]: Map insertion order, not handle order. */
  int ordered[64];
  size_t ordered_count = 0;
  for (int handle = 0; handle < 64; handle++) {
    if (!scripts->occupied[handle]) continue;
    size_t at = ordered_count++;
    while (at > 0 && scripts->order[ordered[at - 1]] > scripts->order[handle]) {
      ordered[at] = ordered[at - 1];
      at--;
    }
    ordered[at] = handle;
  }
  for (size_t position = 0; position < ordered_count; position++) {
    int handle = ordered[position];
    qa_save_value *row = qa_save_value_object();
    qa_save_value *handle_value = qa_save_value_number(handle);
    qa_save_value *state =
        qa_ui_script_source_reader_capture_save_state(scripts->readers[handle], &capture);
    if (row == NULL || handle_value == NULL || state == NULL ||
        !qa_save_value_set(row, "handle", handle_value) ||
        !qa_save_value_set(row, "state", state) ||
        !qa_save_value_push(readers, row)) {
      qa_save_value_free(row);
      qa_save_value_free(handle_value);
      qa_save_value_free(state);
      qa_save_value_free(readers);
      qa_save_value_free(globals);
      qa_save_value_free(image);
      qa_save_value_free(object);
      qa_bot_memory_capture_destroy(&bot_capture);
      return NULL;
    }
  }
  if (!qa_save_value_set(object, "memory", image) ||
      !qa_save_value_set(object, "globals", globals) ||
      !qa_save_value_set(object, "readers", readers)) {
    qa_save_value_free(object);
    qa_save_value_free(image);
    qa_save_value_free(globals);
    qa_save_value_free(readers);
    qa_bot_memory_capture_destroy(&bot_capture);
    return NULL;
  }
  qa_bot_memory_capture_destroy(&bot_capture);
  return object;
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
  qa_bot_memory_restored restored;
  qa_bot_memory_restore(&scripts->bot_memory, memory_reader.value, &restored);
  qa_save_reader_destroy(&memory_reader);
  for (size_t i = 0; i < restored.count; i++)
    if (!track(scripts, restored.allocations[i])) qa_range_error("out of memory");
  qa_ui_script_memory_restore restore = {&restored, restored_allocation};
  reader_field(&reader, "globals", &globals_reader);
  qa_ui_script_global_defines_restore_owned_save_state(scripts->services.globals,
                                                       globals_reader.value);
  qa_save_reader_destroy(&globals_reader);
  reader_field(&reader, "readers", &readers_reader);
  size_t count = qa_save_reader_list_count(&readers_reader);
  qa_ui_script_preprocessor_options options = reader_options(scripts);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, handle_reader, state_reader;
    if (!qa_save_reader_item(&readers_reader, i, &item)) qa_range_error("out of memory");
    reader_field(&item, "handle", &handle_reader);
    double handle = qa_save_reader_integer_min(&handle_reader, 1);
    qa_save_reader_destroy(&handle_reader);
    if (handle >= 64 || scripts->occupied[(size_t)handle])
      qa_save_reader_fail(&reader, "invalid client script slot");
    reader_field(&item, "state", &state_reader);
    qa_ui_script_source_reader *restored_reader =
        qa_ui_script_source_reader_restore_save_state(state_reader.value, resolve_include,
                                                      scripts, &options, &restore);
    qa_save_reader_destroy(&state_reader);
    if (restored_reader == NULL) qa_range_error("out of memory");
    scripts->readers[(size_t)handle] = restored_reader;
    scripts->occupied[(size_t)handle] = true;
    scripts->order[(size_t)handle] = scripts->next_order++;
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&readers_reader);
  qa_save_reader_destroy(&reader);
  qa_bot_memory_restored_destroy(&restored);
}

void qa_qvm_client_scripts_close_all(qa_qvm_client_scripts *scripts) {
  if (scripts->closed) return;
  scripts->closed = true;
  for (int i = 0; i < 64; i++) {
    if (scripts->occupied[i]) qa_ui_script_source_reader_destroy(scripts->readers[i]);
    scripts->occupied[i] = false;
    scripts->pending[i] = false;
  }
  if (scripts->operations == 0) mem_dispose(scripts);
}

double qa_qvm_client_scripts_add_define(qa_qvm_client_scripts *scripts, const char *text) {
  qa_qvm_client_scripts_assert_current(scripts);
  return qa_ui_script_global_defines_add(scripts->services.globals, text) ? 1 : 0;
}

double qa_qvm_client_scripts_load(qa_qvm_client_scripts *scripts, const char *filename) {
  qa_qvm_client_scripts_assert_current(scripts);
  int handle = 1;
  while (handle < 64 && (scripts->occupied[handle] || scripts->pending[handle])) handle++;
  if (handle == 64) return 0;
  scripts->pending[handle] = true;
  scripts->operations++;
  qa_ui_source_script_storage *root = load_source(scripts, filename);
  if (root == NULL) {
    scripts->pending[handle] = false;
    completed(scripts);
    return 0;
  }
  /* TS disposes the root when assertCurrent fails after loading;
   * the C assert aborts, so only the OOM path below disposes it. */
  qa_qvm_client_scripts_assert_current(scripts);
  qa_ui_script_source source;
  memset(&source, 0, sizeof source);
  source.path = filename;
  source.storage = root;
  qa_ui_script_preprocessor_options options = reader_options(scripts);
  qa_ui_script_source_reader *reader =
      qa_ui_script_source_reader_open(&source, resolve_include, scripts, &options);
  if (reader == NULL) {
    qa_ui_source_script_destroy(root);
    scripts->pending[handle] = false;
    completed(scripts);
    qa_range_error("out of memory");
  }
  scripts->readers[handle] = reader;
  scripts->occupied[handle] = true;
  scripts->order[handle] = scripts->next_order++;
  scripts->pending[handle] = false;
  completed(scripts);
  return handle;
}

/* TS readers is a Map<number, ...>: fractional/NaN handles miss. */
static bool occupied_handle(const qa_qvm_client_scripts *scripts, double handle) {
  return handle >= 0 && handle < 64 && handle == trunc(handle) &&
         scripts->occupied[(size_t)handle];
}

double qa_qvm_client_scripts_free(qa_qvm_client_scripts *scripts, double handle) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (!occupied_handle(scripts, handle)) return 0;
  qa_ui_script_source_reader_destroy(scripts->readers[(size_t)handle]);
  scripts->occupied[(size_t)handle] = false;
  return 1;
}

static void validate_current(void *ctx) { qa_qvm_client_scripts_assert_current(ctx); }

typedef struct read_next_ctx {
  qa_qvm_client_scripts *scripts;
  qa_ui_script_source_reader *reader;
  qa_ui_script_token_record record;
  bool read;
} read_next_ctx;

static void run_read_next(void *ctx) {
  read_next_ctx *next = ctx;
  next->read = qa_ui_script_source_reader_next_async(
      next->reader, validate_current, next->scripts, &next->record);
}

double qa_qvm_client_scripts_read(qa_qvm_client_scripts *scripts, double handle,
                                  qa_qvm_script_publish publish) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (!occupied_handle(scripts, handle)) return 0;
  qa_ui_script_source_reader *reader = scripts->readers[(size_t)handle];
  scripts->operations++;
  /* A TS source failure is caught here: read=false, and the partial
   * token is still published below. */
  read_next_ctx next = {scripts, reader, {0}, false};
  bool completed_read = qa_ui_script_source_reader_catch(reader, run_read_next, &next);
  bool read = completed_read && next.read;
  qa_qvm_client_scripts_assert_current(scripts);
  if (!scripts->occupied[(size_t)handle] || scripts->readers[(size_t)handle] != reader) {
    if (read) qa_ui_script_token_record_release(&next.record);
    completed(scripts);
    qa_range_error("QVM source handle was freed during token read");
  }
  publish.call(publish.ctx, reader);
  if (read) qa_ui_script_token_record_release(&next.record);
  completed(scripts);
  return read ? 1 : 0;
}

bool qa_qvm_client_scripts_position(qa_qvm_client_scripts *scripts, double handle,
                                    qa_qvm_script_position *out) {
  qa_qvm_client_scripts_assert_current(scripts);
  if (!occupied_handle(scripts, handle)) return false;
  qa_ui_script_source_reader_position(scripts->readers[(size_t)handle], out->filename,
                                      sizeof out->filename, &out->line);
  return true;
}

typedef struct token_publish_ctx {
  const qa_qvm_host_call *call;
  int32_t pointer;
} token_publish_ctx;

static void publish_token(void *ctx, qa_ui_script_source_reader *reader) {
  token_publish_ctx *publish = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(publish->call->guest, publish->pointer, QA_QVM_SCRIPT_TOKEN_BYTES, 0, &view);
  qa_qvm_write_script_token_memory(&view, qa_ui_script_source_reader_raw_token(reader));
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
      qa_qvm_script_position position;
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
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32((double)position.line), true);
      *out = 1;
      return true;
    }
    default: return false;
  }
}
