/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/foundation/support.h"
#include <string.h>
void qa_bot_script_sources_init(qa_bot_script_sources *s) { memset(s, 0, sizeof *s); }
void qa_bot_script_sources_destroy(qa_bot_script_sources *s) { (void)s; }
static void live(qa_bot_script_sources *s) {
  if (s->terminal)
    qa_range_error("bot script source owner is shut down");
}
void qa_bot_script_sources_set_base_folder(qa_bot_script_sources *s, const char *path) {
  live(s);
  for (const char *p = path; *p; p++)
    if (*p == '%' && p[1] != '%')
      qa_range_error("PC_SetBaseFolder format conversion requires absent source arguments");
  if (strlen(path) >= QA_BOT_MAX_SOURCE_PATH)
    qa_range_error("bot source filenames must contain source bytes");
  /* TS collapses %% to % via Com_sprintf format; reproduce. */
  size_t w = 0;
  for (size_t i = 0; path[i] && w < sizeof s->base_folder - 1; i++) {
    s->base_folder[w++] = path[i];
    if (path[i] == '%' && path[i + 1] == '%')
      i++;
  }
  s->base_folder[w] = '\0';
}
bool qa_bot_script_sources_resolve_root(qa_bot_script_sources *s, const char *path,
                                        qa_script_source_bots_a *out) {
  qa_bot_script_sources_set_base_folder(s, "botfiles");
  if (!s->read_file)
    return false;
  size_t n = 0;
  const uint8_t *b = s->read_file(s->read_ctx, path, &n);
  if (!b)
    return false;
  out->path = path;
  out->bytes = b;
  out->length = n;
  return true;
}
int64_t qa_bot_script_sources_load(qa_bot_script_sources *s, const char *path) {
  live(s);
  (void)path;
  for (int i = 1; i < QA_BOT_MAX_SOURCE_FILES; i++)
    if (!s->handles[i]) {
      s->handles[i] = (void *)(intptr_t)1;
      return i;
    }
  return 0;
}
void qa_bot_script_sources_free(qa_bot_script_sources *s, int64_t h) {
  if (h > 0 && h < QA_BOT_MAX_SOURCE_FILES)
    s->handles[h] = NULL;
}
