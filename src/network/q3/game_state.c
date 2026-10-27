/* Port of src/network/q3/game-state.ts. GPL-2.0-or-later. */
#include "qa/network/q3/game_state.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
struct qa_q3_client_game_state_storage {
  int32_t offsets[1024];
  uint8_t data[16000];
  int64_t count;
  qa_q3_game_state_drop_fn drop; void *drop_ctx;
};
static void do_drop(const qa_q3_client_game_state_storage *s, const char *msg) {
  if (s->drop) { s->drop(s->drop_ctx, msg); qa_range_error("%s", msg); }
  qa_common_error(QA_COMMON_ERROR_DROP, msg);
}
void qa_q3_source_game_state_record_destroy(qa_q3_source_game_state_record *r) { free(r->string_offsets); free(r->string_data); r->string_offsets = NULL; r->string_data = NULL; }
bool qa_q3_client_game_state_storage_init(qa_q3_client_game_state_storage **out, qa_q3_game_state_drop_fn drop, void *drop_ctx) {
  qa_q3_client_game_state_storage *s = calloc(1, sizeof *s);
  if (!s) return false;
  s->drop = drop; s->drop_ctx = drop_ctx;
  *out = s; return true;
}
void qa_q3_client_game_state_storage_destroy(qa_q3_client_game_state_storage *s) { free(s); }
void qa_q3_client_game_state_storage_clear(qa_q3_client_game_state_storage *s) {
  memset(s->offsets, 0, sizeof s->offsets); memset(s->data, 0, sizeof s->data); s->count = 0;
}
void qa_q3_client_game_state_storage_begin_entries(qa_q3_client_game_state_storage *s) { s->count = 1; }
bool qa_q3_client_game_state_storage_copy_source_record(const qa_q3_client_game_state_storage *s, qa_q3_source_game_state_record *out) {
  out->string_offsets = malloc(sizeof s->offsets);
  out->string_data = malloc(sizeof s->data);
  if (!out->string_offsets || !out->string_data) { free(out->string_offsets); free(out->string_data); return false; }
  memcpy(out->string_offsets, s->offsets, sizeof s->offsets);
  memcpy(out->string_data, s->data, sizeof s->data);
  out->data_count = s->count;
  return true;
}
bool qa_q3_client_game_state_storage_get(const qa_q3_client_game_state_storage *s, int64_t index, char **out) {
  *out = NULL;
  if (index < 0 || index >= 1024) qa_range_error("Invalid configstring index %lld", (long long)index);
  int32_t offset = s->offsets[index];
  if (offset == 0) return true;
  size_t n = 0;
  for (int64_t cursor = offset; cursor < 16000; cursor++) {
    if (s->data[cursor] == 0) {
      *out = malloc(n + 1);
      if (!*out) return false;
      memcpy(*out, s->data + offset, n); (*out)[n] = 0; return true;
    }
    n++;
  }
  qa_range_error("Unterminated owned configstring");
}
bool qa_q3_client_game_state_storage_copy_strings(const qa_q3_client_game_state_storage *s, char ***out, size_t *count) {
  *out = calloc(1024, sizeof(char *));
  if (!*out) return false;
  *count = 1024;
  for (int i = 0; i < 1024; i++) {
    char *v = NULL;
    if (!qa_q3_client_game_state_storage_get(s, i, &v)) return false;
    if (!v) { v = malloc(1); if (!v) return false; v[0] = 0; }
    (*out)[i] = v;
  }
  return true;
}
static void validate(const qa_q3_client_game_state_storage *s, int64_t index, const char *value) {
  if (index < 0 || index >= 1024) do_drop(s, "configstring > MAX_CONFIGSTRINGS");
  (void)value; /* C bytes are non-NUL source bytes within the string */
}
static void append_bytes(qa_q3_client_game_state_storage *s, int64_t index, const char *value) {
  size_t n = strlen(value);
  if ((int64_t)n + 1 + s->count > 16000) do_drop(s, "MAX_GAMESTATE_CHARS exceeded");
  s->offsets[index] = (int32_t)s->count;
  memcpy(s->data + s->count, value, n);
  s->data[s->count + (int64_t)n] = 0;
  s->count += (int64_t)n + 1;
}
void qa_q3_client_game_state_storage_append(qa_q3_client_game_state_storage *s, int64_t index, const char *value) {
  validate(s, index, value);
  append_bytes(s, index, value);
}
bool qa_q3_client_game_state_storage_modify(qa_q3_client_game_state_storage *s, int64_t index, const char *value) {
  validate(s, index, value);
  char *current = NULL;
  qa_q3_client_game_state_storage_get(s, index, &current);
  bool same = strcmp(current ? current : "", value) == 0;
  free(current);
  if (same) return false;
  char **previous = NULL; size_t count = 0;
  if (!qa_q3_client_game_state_storage_copy_strings(s, &previous, &count)) qa_range_error("game state copy allocation failed");
  qa_q3_client_game_state_storage_clear(s);
  qa_q3_client_game_state_storage_begin_entries(s);
  for (size_t slot = 0; slot < count; slot++) {
    const char *text = (int64_t)slot == index ? value : previous[slot];
    if (text[0] != 0) append_bytes(s, (int64_t)slot, text);
  }
  for (size_t i = 0; i < count; i++) free(previous[i]);
  free(previous);
  return true;
}
