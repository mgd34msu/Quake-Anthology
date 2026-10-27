/* Port of src/bots/behavior/library/script-sources.ts. Script lexer/preprocessor
 * types are owned by ui/common/legacy/script (bots-b/other slices); mirrored here
 * as opaque handles + fn+ctx reader. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_SCRIPT_SOURCES_H
#define QA_BOTS_BEHAVIOR_LIBRARY_SCRIPT_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_BOT_MAX_SOURCE_FILES 64
#define QA_BOT_MAX_SOURCE_PATH 64
/* TS ScriptSource: borrowed text + path. */
typedef struct qa_script_source_bots_a {
  const char *path;
  const uint8_t *bytes;
  size_t length;
} qa_script_source_bots_a;
typedef struct qa_bot_script_sources {
  void *handles[QA_BOT_MAX_SOURCE_FILES];
  char base_folder[QA_BOT_MAX_SOURCE_PATH];
  bool terminal;
  const uint8_t *(*read_file)(void *ctx, const char *path, size_t *out_len);
  void *read_ctx;
  void (*print)(void *ctx, int severity, const char *text);
  void *print_ctx;
} qa_bot_script_sources;
void qa_bot_script_sources_init(qa_bot_script_sources *s);
void qa_bot_script_sources_destroy(qa_bot_script_sources *s);
void qa_bot_script_sources_set_base_folder(qa_bot_script_sources *s, const char *path);
bool qa_bot_script_sources_resolve_root(qa_bot_script_sources *s, const char *path,
                                        qa_script_source_bots_a *out);
int64_t qa_bot_script_sources_load(qa_bot_script_sources *s, const char *path);
void qa_bot_script_sources_free(qa_bot_script_sources *s, int64_t handle);
#endif
