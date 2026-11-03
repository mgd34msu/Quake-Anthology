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
  /* TS drop is `(message) => never`; a returning host drop still ends the call. */
  if (s->drop) s->drop(s->drop_ctx, msg);
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
typedef struct { char **strings; size_t count; } string_list;
static void string_list_release(void *arg) {
  string_list *list = arg;
  for (size_t i = 0; i < list->count; i++) free(list->strings[i]);
  free(list->strings);
  list->strings = NULL; list->count = 0;
}
bool qa_q3_client_game_state_storage_copy_strings(const qa_q3_client_game_state_storage *s, char ***out, size_t *count) {
  string_list list = { calloc(1024, sizeof(char *)), 0 };
  if (!list.strings) return false;
  qa_cleanup guard;
  qa_cleanup_push(&guard, string_list_release, &list);
  bool ok = true;
  for (int i = 0; i < 1024 && ok; i++) {
    char *v = NULL;
    if (!qa_q3_client_game_state_storage_get(s, i, &v)) { ok = false; break; }
    if (!v) { v = malloc(1); if (!v) { ok = false; break; } v[0] = 0; }
    list.strings[list.count++] = v;
  }
  qa_cleanup_pop(&guard);
  if (!ok) { string_list_release(&list); return false; }
  *out = list.strings; *count = list.count;
  return true;
}
/* validate(index, value): C strings carry no NUL and only byte characters, so
 * the TS RangeError on NUL/>255 has no C input that reaches it. */
static void validate(const qa_q3_client_game_state_storage *s, int64_t index) {
  if (index < 0 || index >= 1024) do_drop(s, "configstring > MAX_CONFIGSTRINGS");
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
  validate(s, index);
  append_bytes(s, index, value);
}
bool qa_q3_client_game_state_storage_modify(qa_q3_client_game_state_storage *s, int64_t index, const char *value) {
  validate(s, index);
  char *current = NULL;
  if (!qa_q3_client_game_state_storage_get(s, index, &current)) qa_range_error("game state copy allocation failed");
  bool same = strcmp(current ? current : "", value) == 0;
  free(current);
  if (same) return false;
  string_list previous = { NULL, 0 };
  if (!qa_q3_client_game_state_storage_copy_strings(s, &previous.strings, &previous.count))
    qa_range_error("game state copy allocation failed");
  /* append_bytes drops (raises) on MAX_GAMESTATE_CHARS mid-rebuild. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, string_list_release, &previous);
  qa_q3_client_game_state_storage_clear(s);
  qa_q3_client_game_state_storage_begin_entries(s);
  for (size_t slot = 0; slot < previous.count; slot++) {
    const char *text = (int64_t)slot == index ? value : previous.strings[slot];
    if (text[0] != 0) append_bytes(s, (int64_t)slot, text);
  }
  qa_cleanup_pop(&guard);
  string_list_release(&previous);
  return true;
}
