/* Port of src/app/bootstrap/image-reader.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/image_reader.h"
#include <stdlib.h>
#include <string.h>
typedef struct { char *path; const void *mount; const char *content; bool known; } winner_row;
struct qa_app_mounted_image_reader {
  qa_app_image_reader_host host;
  winner_row *winners; size_t winner_count, winner_cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_mounted_image_reader *qa_app_mounted_image_reader_create(qa_app_image_reader_host host) {
  qa_app_mounted_image_reader *reader = calloc(1, sizeof *reader);
  if (!reader) return NULL;
  reader->host = host;
  return reader;
}
void qa_app_mounted_image_reader_destroy(qa_app_mounted_image_reader *reader) {
  if (!reader) return;
  for (size_t i = 0; i < reader->winner_count; i++) free(reader->winners[i].path);
  free(reader->winners);
  free(reader);
}
static winner_row *winner_for(qa_app_mounted_image_reader *reader, const char *path, bool create) {
  for (size_t i = 0; i < reader->winner_count; i++)
    if (strcmp(reader->winners[i].path, path) == 0) return &reader->winners[i];
  if (!create) return NULL;
  if (reader->winner_count == reader->winner_cap) {
    reader->winner_cap = reader->winner_cap ? reader->winner_cap * 2 : 16;
    reader->winners = realloc(reader->winners, reader->winner_cap * sizeof *reader->winners);
  }
  winner_row *row = &reader->winners[reader->winner_count++];
  *row = (winner_row){dup(path), NULL, NULL, false};
  return row;
}
bool qa_app_mounted_image_reader_read(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out) {
  const void *mount = NULL;
  const char *content = NULL;
  bool found = reader->host.open(reader->host.ctx, path, NULL, false, &out->bytes, &out->byte_count, &mount, &content);
  winner_row *row = winner_for(reader, path, true);
  row->mount = found ? mount : NULL;
  row->content = content;
  row->known = true;
  return found;
}
bool qa_app_mounted_image_reader_read_original(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out) {
  reader->host.assert_open(reader->host.ctx);
  winner_row *row = winner_for(reader, path, false);
  if (!row || !row->known) {
    uint8_t *bytes = NULL; size_t count = 0;
    const void *mount = NULL;
    const char *content = NULL;
    bool found = reader->host.open(reader->host.ctx, path, NULL, false, &bytes, &count, &mount, &content);
    free(bytes);
    row = winner_for(reader, path, true);
    row->mount = found ? mount : NULL;
    row->content = content;
    row->known = true;
  }
  if (!row->mount || !reader->host.is_user_mount(reader->host.ctx, row->mount)) return false;
  const void *mount = NULL;
  const char *content = NULL;
  return reader->host.open(reader->host.ctx, path, row->content, true, &out->bytes, &out->byte_count, &mount, &content);
}
