/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/source_files.h"
#include <stdlib.h>
#include <string.h>
char *qa_read_bot_source_text(qa_bot_asset_files *files, const char *path) {
  int64_t len = qa_bot_asset_files_read(files, path, NULL, 0); if (len < 0) return NULL;
  uint8_t *bytes = malloc(len ? (size_t)len : 1); if (!bytes) return NULL;
  if (len) (void)qa_bot_asset_files_read(files, path, bytes, (size_t)len);
  size_t end = (size_t)len; while (end > 0 && bytes[end - 1] == 0) end--;
  char *text = malloc(end + 1); if (!text) { free(bytes); return NULL;
  }
  for (size_t i = 0; i < end; i++) text[i] = (char)bytes[i];
  text[end] = 0; free(bytes); return text;
}
