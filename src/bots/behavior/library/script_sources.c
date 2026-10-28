/* See include/qa/bots/behavior/library/script_sources.h.
 * GPL-2.0-or-later. */
#include "qa/bots/behavior/library/script_sources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool mem_allocate(void *ctx, size_t size, bool clear,
                         qa_ui_script_allocation *out) {
  qa_bot_script_sources *s = ctx;
  qa_bot_memory_allocation bot;
  if (!qa_bot_memory_allocate(&s->memory, (int64_t)size, QA_BOT_MEMORY_HEAP,
                              clear, &bot))
    return false;
  out->bytes = bot.bytes;
  out->length = bot.length;
  out->record = bot.record;
  return true;
}

static void mem_free(void *ctx, qa_ui_script_allocation *allocation) {
  qa_bot_script_sources *s = ctx;
  qa_bot_memory_allocation bot = {allocation->bytes, allocation->length,
                                   allocation->record};
  qa_bot_memory_free(&s->memory, &bot);
}

void qa_bot_script_sources_init(qa_bot_script_sources *s) {
  memset(s, 0, sizeof *s);
  qa_bot_memory_init(&s->memory);
  s->script_memory.ctx = s;
  s->script_memory.allocate = mem_allocate;
  s->script_memory.free_allocation = mem_free;
}

void qa_bot_script_sources_dispose_resources(qa_bot_script_sources *s) {
  if (s->terminal) return;
  s->terminal = true;
  for (int i = 0; i < QA_BOT_MAX_SOURCE_FILES; i++) {
    if (s->handles[i] != NULL) qa_ui_script_source_reader_destroy(s->handles[i]);
    s->handles[i] = NULL;
  }
}

void qa_bot_script_sources_destroy(qa_bot_script_sources *s) {
  qa_bot_script_sources_dispose_resources(s);
  qa_bot_memory_destroy(&s->memory);
}

static void live(qa_bot_script_sources *s) {
  if (s->terminal)
    qa_range_error("Bot script source resources have been disposed");
}

/* TS boundedPath: writes the 63-byte slice into out. */
static void bounded_path(qa_bot_script_sources *s, const char *text,
                         char out[QA_BOT_MAX_SOURCE_PATH]) {
  size_t length = strlen(text);
  if (length >= 32000)
    qa_range_error("Com_sprintf formatted bot path exceeds its 32000-byte source buffer");
  if (length >= QA_BOT_MAX_SOURCE_PATH) {
    if (s->common_print != NULL) {
      char message[96];
      snprintf(message, sizeof message, "Com_sprintf: overflow of %zu in %d\n",
               length, QA_BOT_MAX_SOURCE_PATH);
      s->common_print(s->common_print_ctx, message);
    }
    live(s);
  }
  size_t kept = length < QA_BOT_MAX_SOURCE_PATH - 1 ? length : QA_BOT_MAX_SOURCE_PATH - 1;
  memcpy(out, text, kept);
  out[kept] = '\0';
}

void qa_bot_script_sources_set_base_folder(qa_bot_script_sources *s, const char *path) {
  live(s);
  /* TS sourceString byte validation is inherent to C byte strings.
   * The folder build runs over the full string before boundedPath,
   * so a % beyond byte 63 still raises. */
  size_t length = strlen(path);
  char *folder = malloc(length + 1);
  if (folder == NULL) qa_range_error("out of memory");
  size_t used = 0;
  for (size_t i = 0; i < length; i++) {
    char c = path[i];
    if (c == '%') {
      if (path[i + 1] != '%') {
        free(folder);
        qa_range_error("PC_SetBaseFolder format conversion requires absent source arguments");
      }
      i++;
    }
    folder[used++] = c;
  }
  folder[used] = '\0';
  bounded_path(s, folder, s->base_folder);
  free(folder);
}

static void report_diagnostic(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  qa_bot_script_sources *s = ctx;
  live(s);
  if (s->print != NULL) {
    int length = snprintf(NULL, 0, "file %s, line %lld: %s\n",
                          diagnostic->location.path,
                          (long long)diagnostic->location.line, diagnostic->message);
    char *message = malloc((size_t)length + 1);
    if (message == NULL) qa_range_error("out of memory");
    snprintf(message, (size_t)length + 1, "file %s, line %lld: %s\n",
             diagnostic->location.path, (long long)diagnostic->location.line,
             diagnostic->message);
    s->print(s->print_ctx,
             diagnostic->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? 2 : 3, message);
    free(message);
  }
  live(s);
}

static bool resolve_include(void *ctx, const qa_ui_script_include_request *request,
                            qa_ui_script_source *out) {
  return qa_bot_script_sources_resolve(ctx, request, out);
}

/* TS readFile; NULL is the TS undefined source. */
static qa_ui_source_script_storage *read_file(qa_bot_script_sources *s, const char *path) {
  live(s);
  char asset_path[QA_BOT_MAX_SOURCE_PATH];
  if (s->base_folder[0] == '\0') {
    bounded_path(s, path, asset_path);
  } else {
    size_t length = strlen(s->base_folder) + 1 + strlen(path);
    char *joined = malloc(length + 1);
    if (joined == NULL) qa_range_error("out of memory");
    snprintf(joined, length + 1, "%s/%s", s->base_folder, path);
    bounded_path(s, joined, asset_path);
    free(joined);
  }
  if (s->read_file == NULL) return NULL;
  size_t opened_length = 0;
  const uint8_t *opened = s->read_file(s->read_ctx, asset_path, &opened_length);
  live(s);
  if (opened == NULL) return NULL;
  qa_ui_source_script_storage *source =
      qa_ui_script_allocate_source((int64_t)opened_length, path, &s->script_memory);
  if (source == NULL) qa_range_error("out of memory");
  size_t buffer_length = 0;
  uint8_t *buffer = qa_ui_source_script_buffer(source, &buffer_length);
  memcpy(buffer, opened, opened_length);
  live(s);
  qa_ui_source_script_compress(source);
  return source;
}

bool qa_bot_script_sources_resolve_root(qa_bot_script_sources *s, const char *path,
                                        qa_ui_script_source *out) {
  qa_bot_script_sources_set_base_folder(s, "botfiles");
  qa_ui_source_script_storage *storage = read_file(s, path);
  if (storage == NULL) return false;
  memset(out, 0, sizeof *out);
  out->path = path;
  out->storage = storage;
  return true;
}

bool qa_bot_script_sources_resolve(qa_bot_script_sources *s,
                                   const qa_ui_script_include_request *request,
                                   qa_ui_script_source *out) {
  live(s);
  bool system = request->kind == QA_UI_SCRIPT_INCLUDE_SYSTEM;
  if (system && strlen(request->requested_path) >= QA_BOT_MAX_SOURCE_PATH)
    qa_range_error("PC_Directive_include system filename exceeds its 64-byte source path");
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
  qa_ui_source_script_storage *storage = read_file(s, filename);
  if (storage == NULL && !system) {
    const char *prefix = request->include_path == NULL ? "" : request->include_path;
    size_t retry_length = strlen(prefix) + strlen(filename);
    if (retry_length >= QA_BOT_MAX_SOURCE_PATH) {
      free(filename);
      qa_range_error("PC_Directive_include quoted retry exceeds its 64-byte source path");
    }
    char *retry = malloc(retry_length + 1);
    if (retry == NULL) qa_range_error("out of memory");
    strcpy(retry, prefix);
    strcat(retry, filename);
    storage = read_file(s, retry);
    free(retry);
  }
  free(filename);
  if (storage == NULL) return false;
  memset(out, 0, sizeof *out);
  out->path = request->requested_path;
  out->storage = storage;
  return true;
}

int64_t qa_bot_script_sources_load(qa_bot_script_sources *s, const char *path) {
  live(s);
  int handle = 1;
  while (handle < QA_BOT_MAX_SOURCE_FILES && s->handles[handle] != NULL) handle++;
  if (handle == QA_BOT_MAX_SOURCE_FILES) return 0;
  qa_bot_script_sources_set_base_folder(s, "");
  qa_ui_source_script_storage *root = read_file(s, path);
  if (root == NULL) return 0;
  qa_ui_script_source source;
  memset(&source, 0, sizeof source);
  source.path = path;
  source.storage = root;
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = s->globals;
  if (s->debug_eval != NULL) {
    options.debug_eval = s->debug_eval;
    options.debug_eval_ctx = s->debug_eval_ctx;
  }
  options.report = report_diagnostic;
  options.report_ctx = s;
  qa_ui_script_source_reader *reader =
      qa_ui_script_source_reader_open(&source, resolve_include, s, &options);
  if (reader == NULL) {
    qa_ui_source_script_destroy(root);
    qa_range_error("out of memory");
  }
  s->handles[handle] = reader;
  return handle;
}

bool qa_bot_script_sources_free(qa_bot_script_sources *s, int64_t handle) {
  live(s);
  if (handle < 1 || handle >= QA_BOT_MAX_SOURCE_FILES) return false;
  if (s->handles[handle] == NULL) return false;
  qa_ui_script_source_reader_destroy(s->handles[handle]);
  s->handles[handle] = NULL;
  return true;
}

bool qa_bot_script_sources_read_token_result(qa_bot_script_sources *s, int64_t handle,
                                             qa_ui_source_token_memory **out_token,
                                             bool *out_read) {
  live(s);
  if (handle < 1 || handle >= QA_BOT_MAX_SOURCE_FILES) return false;
  qa_ui_script_source_reader *source = s->handles[handle];
  if (source == NULL) return false;
  /* A TS source failure returns {read:false, token}; the C engine
   * aborts uncaught failures instead. */
  qa_ui_script_token_record record;
  bool read = qa_ui_script_source_reader_next(source, &record);
  if (read) qa_ui_script_token_record_release(&record);
  *out_token = qa_ui_script_source_reader_raw_token(source);
  *out_read = read;
  return true;
}

bool qa_bot_script_sources_read_token(qa_bot_script_sources *s, int64_t handle,
                                      qa_ui_script_token_record *out) {
  qa_ui_source_token_memory *token = NULL;
  bool read = false;
  if (!qa_bot_script_sources_read_token_result(s, handle, &token, &read) || !read)
    return false;
  qa_ui_script_source_reader *source = s->handles[handle];
  if (source == NULL)
    qa_range_error("script handle retired during a completed read");
  qa_ui_script_source_reader_current_record(source, out);
  return true;
}

bool qa_bot_script_sources_file_and_line(qa_bot_script_sources *s, int64_t handle,
                                         char filename[QA_BOT_MAX_SOURCE_PATH + 1],
                                         int64_t *out_line) {
  live(s);
  if (handle < 1 || handle >= QA_BOT_MAX_SOURCE_FILES) return false;
  qa_ui_script_source_reader *source = s->handles[handle];
  if (source == NULL) return false;
  char full[1024];
  qa_ui_script_source_reader_position(source, full, sizeof full, out_line);
  size_t length = strlen(full);
  if (length > QA_BOT_MAX_SOURCE_PATH) length = QA_BOT_MAX_SOURCE_PATH;
  memcpy(filename, full, length);
  filename[length] = '\0';
  return true;
}

void qa_bot_script_sources_check_open(qa_bot_script_sources *s) {
  live(s);
  for (int handle = 1; handle < QA_BOT_MAX_SOURCE_FILES; handle++) {
    qa_ui_script_source_reader *source = s->handles[handle];
    if (source == NULL) continue;
    if (s->print != NULL) {
      char name[1024];
      qa_ui_script_source_reader_current_filename(source, name, sizeof name);
      int length = snprintf(NULL, 0, "file %s still open in precompiler\n", name);
      char *message = malloc((size_t)length + 1);
      if (message == NULL) qa_range_error("out of memory");
      snprintf(message, (size_t)length + 1, "file %s still open in precompiler\n", name);
      s->print(s->print_ctx, 3, message);
      free(message);
    }
    live(s);
  }
}

qa_save_value *qa_bot_script_sources_capture_save_state(
    qa_bot_script_sources *s, const qa_ui_script_memory_capture *capture) {
  if (s->globals == NULL)
    qa_range_error("Bot script sources have no script globals");
  qa_save_value *object = qa_save_value_object();
  qa_save_value *folder = qa_save_value_string(s->base_folder);
  qa_save_value *terminal = qa_save_value_boolean(s->terminal);
  qa_save_value *globals = qa_ui_script_global_defines_capture_save_state(s->globals, capture);
  qa_save_value *files = qa_save_value_array();
  if (object == NULL || folder == NULL || terminal == NULL || globals == NULL ||
      files == NULL ||
      !qa_save_value_set(object, "baseFolder", folder) ||
      !qa_save_value_set(object, "terminal", terminal) ||
      !qa_save_value_set(object, "globals", globals)) {
    qa_save_value_free(object);
    qa_save_value_free(folder);
    qa_save_value_free(terminal);
    qa_save_value_free(globals);
    qa_save_value_free(files);
    return NULL;
  }
  for (int i = 0; i < QA_BOT_MAX_SOURCE_FILES; i++) {
    qa_save_value *cell = s->handles[i] == NULL
                              ? qa_save_value_null()
                              : qa_ui_script_source_reader_capture_save_state(
                                    s->handles[i], capture);
    if (cell == NULL || !qa_save_value_push(files, cell)) {
      qa_save_value_free(cell);
      qa_save_value_free(files);
      qa_save_value_free(object);
      return NULL;
    }
  }
  if (!qa_save_value_set(object, "sourceFiles", files)) {
    qa_save_value_free(files);
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

static void reader_field(const qa_save_reader *reader, const char *name,
                         qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

void qa_bot_script_sources_restore_save_state(
    qa_bot_script_sources *s, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore) {
  for (int i = 0; i < QA_BOT_MAX_SOURCE_FILES; i++)
    if (s->handles[i] != NULL)
      qa_range_error("Script restore requires an empty handle owner");
  if (s->globals == NULL)
    qa_range_error("Bot script sources have no script globals");
  qa_save_reader reader, folder_reader, terminal_reader, files_reader, globals_reader;
  if (!qa_save_reader_init(&reader, value, "bot.sources")) qa_range_error("out of memory");
  reader_field(&reader, "baseFolder", &folder_reader);
  const char *folder = qa_save_reader_string(&folder_reader);
  qa_save_reader_destroy(&folder_reader);
  reader_field(&reader, "terminal", &terminal_reader);
  bool terminal = qa_save_reader_boolean(&terminal_reader);
  qa_save_reader_destroy(&terminal_reader);
  /* TS sourceString(folder) !== folder is inherent for C strings. */
  if (strlen(folder) >= QA_BOT_MAX_SOURCE_PATH)
    qa_save_reader_fail(&reader, "invalid script base folder");
  reader_field(&reader, "sourceFiles", &files_reader);
  size_t count = qa_save_reader_list_count(&files_reader);
  const qa_save_value *saved[QA_BOT_MAX_SOURCE_FILES];
  memset(saved, 0, sizeof saved);
  bool any_saved = false;
  for (size_t i = 0; i < count && i < QA_BOT_MAX_SOURCE_FILES; i++) {
    qa_save_reader cell;
    if (!qa_save_reader_item(&files_reader, i, &cell)) qa_range_error("out of memory");
    if (!qa_save_reader_is_null(&cell)) {
      saved[i] = cell.value;
      any_saved = true;
    }
    qa_save_reader_destroy(&cell);
  }
  if (count != QA_BOT_MAX_SOURCE_FILES || saved[0] != NULL || (terminal && any_saved))
    qa_save_reader_fail(&reader, "invalid script handle table");
  reader_field(&reader, "globals", &globals_reader);
  qa_ui_script_global_defines_restore_save_state(s->globals, globals_reader.value, restore);
  qa_save_reader_destroy(&globals_reader);
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.memory = &s->script_memory;
  if (s->debug_eval != NULL) {
    options.debug_eval = s->debug_eval;
    options.debug_eval_ctx = s->debug_eval_ctx;
  }
  options.report = report_diagnostic;
  options.report_ctx = s;
  for (int handle = 0; handle < QA_BOT_MAX_SOURCE_FILES; handle++) {
    if (saved[handle] == NULL) continue;
    qa_ui_script_source_reader *restored = qa_ui_script_source_reader_restore_save_state(
        saved[handle], resolve_include, s, &options, restore);
    if (restored == NULL) qa_range_error("out of memory");
    s->handles[handle] = restored;
  }
  qa_save_reader_destroy(&files_reader);
  snprintf(s->base_folder, sizeof s->base_folder, "%s", folder);
  s->terminal = terminal;
  qa_save_reader_destroy(&reader);
}
