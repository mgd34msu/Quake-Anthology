/*
 * Port of src/bots/behavior/library/script-sources.ts
 * (BotScriptSources over the landed ui/common/legacy/script
 * engine). Handles hold real ScriptSourceReaders; ScriptSource is
 * qa_ui_script_source carrying a SourceScriptStorage. The TS
 * default memory (new BotMemory) is the owned qa_bot_memory behind
 * the script_memory adapter; an owner may replace script_memory
 * after init (TS constructor memory argument). globals is borrowed
 * and must be set before load/capture, as must the callbacks the TS
 * constructor requires (assets read, print, commonPrint). The
 * struct address must stay stable while handles are open (engine
 * resolver/report ctx). read_token_result catches engine source
 * failures via qa_ui_script_source_reader_catch and returns
 * read=false with the raw token, as TS readTokenHandleResult.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_SCRIPT_SOURCES_H
#define QA_BOTS_BEHAVIOR_LIBRARY_SCRIPT_SOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/bots/behavior/library/memory.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

#define QA_BOT_MAX_SOURCE_FILES 64
#define QA_BOT_MAX_SOURCE_PATH 64

typedef struct qa_bot_script_sources {
  qa_ui_script_source_reader *handles[QA_BOT_MAX_SOURCE_FILES];
  char base_folder[QA_BOT_MAX_SOURCE_PATH];
  bool terminal;
  qa_ui_script_global_defines *globals; /* borrowed */
  qa_bot_memory memory;
  qa_ui_script_memory script_memory;
  /* BotSourceFiles.read: borrowed bytes, NULL is TS null. */
  const uint8_t *(*read_file)(void *ctx, const char *path, size_t *out_len);
  void *read_ctx;
  void (*print)(void *ctx, int severity, const char *text); /* 2|3 */
  void *print_ctx;
  void (*common_print)(void *ctx, const char *text);
  void *common_print_ctx;
  void (*debug_eval)(void *ctx, const char *text); /* NULL: absent */
  void *debug_eval_ctx;
} qa_bot_script_sources;

void qa_bot_script_sources_init(qa_bot_script_sources *s);
void qa_bot_script_sources_destroy(qa_bot_script_sources *s);
void qa_bot_script_sources_dispose_resources(qa_bot_script_sources *s);
/* Owned checkpoint tree; NULL only on OOM. */
qa_save_value *qa_bot_script_sources_capture_save_state(
    qa_bot_script_sources *s, const qa_ui_script_memory_capture *capture);
void qa_bot_script_sources_restore_save_state(
    qa_bot_script_sources *s, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore);
void qa_bot_script_sources_set_base_folder(qa_bot_script_sources *s, const char *path);
/* On true, out->storage ownership moves to the caller. */
bool qa_bot_script_sources_resolve_root(qa_bot_script_sources *s, const char *path,
                                        qa_ui_script_source *out);
int64_t qa_bot_script_sources_load(qa_bot_script_sources *s, const char *path);
bool qa_bot_script_sources_free(qa_bot_script_sources *s, int64_t handle);
/* False is the TS undefined record; *out holds one token reference. */
bool qa_bot_script_sources_read_token(qa_bot_script_sources *s, int64_t handle,
                                      qa_ui_script_token_record *out);
/* False is the TS undefined result; *out_token is borrowed. */
bool qa_bot_script_sources_read_token_result(qa_bot_script_sources *s, int64_t handle,
                                             qa_ui_source_token_memory **out_token,
                                             bool *out_read);
/* False is the TS undefined position; filename holds 64 chars + NUL. */
bool qa_bot_script_sources_file_and_line(qa_bot_script_sources *s, int64_t handle,
                                         char filename[QA_BOT_MAX_SOURCE_PATH + 1],
                                         int64_t *out_line);
void qa_bot_script_sources_check_open(qa_bot_script_sources *s);
/* TS IncludeResolver.resolve; on true, out->storage moves. */
bool qa_bot_script_sources_resolve(qa_bot_script_sources *s,
                                   const qa_ui_script_include_request *request,
                                   qa_ui_script_source *out);

#endif
