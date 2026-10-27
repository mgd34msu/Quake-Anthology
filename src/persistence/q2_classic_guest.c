/* Port of src/persistence/q2-classic-guest.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_classic_guest.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static uint8_t *dup_bytes(const uint8_t *bytes, size_t length) {
  uint8_t *out = (uint8_t *)malloc(length != 0 ? length : 1);
  if (out != NULL && length != 0) memcpy(out, bytes, length);
  return out;
}
static bool same_text(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}
static bool safe_integer(double value) {
  return value == trunc(value) && fabs(value) <= 9007199254740991.0;
}

/* --- SaveReader helpers (execution.c precedent) --- */
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}

/* --- level-state read/write --- */
static void read_level_state(const qa_save_reader *reader,
                             qa_q2_classic_level_state *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader configstrings = field(reader, "configstrings");
  out->configstring_count = qa_save_reader_list_count(&configstrings);
  out->configstrings = (qa_q2_classic_configstring_entry *)calloc(
      out->configstring_count != 0 ? out->configstring_count : 1,
      sizeof *out->configstrings);
  if (out->configstrings == NULL) qa_save_reader_fail(&configstrings, "out of memory");
  for (size_t i = 0; i < out->configstring_count; i++) {
    qa_save_reader entry = item(&configstrings, i);
    qa_save_reader index = field(&entry, "index");
    out->configstrings[i].index = qa_save_reader_integer_min(&index, 0);
    qa_save_reader_destroy(&index);
    qa_save_reader value = field(&entry, "value");
    out->configstrings[i].value = qa_save_reader_string(&value);
    qa_save_reader_destroy(&value);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&configstrings);
  qa_save_reader portals = field(reader, "portals");
  out->portal_count = qa_save_reader_list_count(&portals);
  out->portals = (qa_q2_classic_portal_entry *)calloc(
      out->portal_count != 0 ? out->portal_count : 1, sizeof *out->portals);
  if (out->portals == NULL) qa_save_reader_fail(&portals, "out of memory");
  for (size_t i = 0; i < out->portal_count; i++) {
    qa_save_reader entry = item(&portals, i);
    qa_save_reader portal = field(&entry, "portal");
    out->portals[i].portal = qa_save_reader_integer_min(&portal, 0);
    qa_save_reader_destroy(&portal);
    qa_save_reader open = field(&entry, "open");
    out->portals[i].open = qa_save_reader_boolean(&open);
    qa_save_reader_destroy(&open);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&portals);
}
static bool build_level_state_into(qa_save_value *obj,
                                   const qa_q2_classic_level_state *state) {
  qa_save_value *configstrings = qa_save_value_array();
  if (configstrings == NULL) return false;
  for (size_t i = 0; i < state->configstring_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL ||
        !set_own(entry, "index", qa_save_value_number(state->configstrings[i].index)) ||
        !set_own(entry, "value", qa_save_value_string(
            state->configstrings[i].value != NULL ? state->configstrings[i].value : "")) ||
        !push_own(configstrings, entry)) {
      qa_save_value_free(entry);
      qa_save_value_free(configstrings);
      return false;
    }
  }
  if (!set_own(obj, "configstrings", configstrings)) return false;
  qa_save_value *portals = qa_save_value_array();
  if (portals == NULL) return false;
  for (size_t i = 0; i < state->portal_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL ||
        !set_own(entry, "portal", qa_save_value_number(state->portals[i].portal)) ||
        !set_own(entry, "open", qa_save_value_boolean(state->portals[i].open)) ||
        !push_own(portals, entry)) {
      qa_save_value_free(entry);
      qa_save_value_free(portals);
      return false;
    }
  }
  return set_own(obj, "portals", portals);
}

/* --- validation --- */
static void validate_level_state(const qa_q2_classic_level_state *state) {
  for (size_t i = 0; i < state->configstring_count; i++) {
    double index = state->configstrings[i].index;
    if (!safe_integer(index) || index < 0 || index >= 2080)
      qa_error("Invalid classic native level metadata");
    for (size_t j = i + 1; j < state->configstring_count; j++)
      if (state->configstrings[j].index == index)
        qa_error("Invalid classic native level metadata");
  }
  for (size_t i = 0; i < state->portal_count; i++) {
    double portal = state->portals[i].portal;
    if (!safe_integer(portal) || portal < 0 || portal >= 1024)
      qa_error("Invalid classic native level metadata");
    for (size_t j = i + 1; j < state->portal_count; j++)
      if (state->portals[j].portal == portal)
        qa_error("Invalid classic native level metadata");
  }
}
static bool valid_visited_map(const char *map, const char *save_map) {
  if (map == NULL) return false;
  /* TS length counts UTF-16 units; C counts bytes. Map paths are
   * ASCII in practice, where the counts agree. */
  size_t length = strlen(map);
  if (length <= 9) return false;
  if (strncmp(map, "maps/", 5) != 0) return false;
  if (strcmp(map + length - 4, ".bsp") != 0) return false;
  for (size_t i = 0; i < length; i++) {
    unsigned char ch = (unsigned char)map[i];
    if (ch < 0x20 || ch == 0x7f || ch == '\\' || ch == ':') return false;
  }
  if (save_map != NULL && strcmp(map, save_map) == 0) return false;
  const char *part = map;
  for (;;) {
    const char *slash = strchr(part, '/');
    size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (part_length == 0) return false;
    if (part_length == 1 && part[0] == '.') return false;
    if (part_length == 2 && part[0] == '.' && part[1] == '.') return false;
    if (slash == NULL) break;
    part = slash + 1;
  }
  return true;
}
static void validate(const qa_q2_classic_original_save *save,
                     qa_q2_classic_original_identity expected) {
  if (!same_text(save->module.id, expected.module.id) ||
      !same_text(save->module.artifact_path, expected.module.artifact_path) ||
      !same_text(save->module.digest, expected.module.digest) ||
      !same_text(save->module.revision, expected.module.revision) ||
      !same_text(save->map, expected.map))
    qa_error("Classic native save differs from the selected module, ABI or map");
  validate_level_state(&save->server.state);
  if (save->server.cvars.length == 0)
    qa_error("Invalid classic native server state");
  for (size_t i = 0; i < save->visited_level_count; i++) {
    const qa_q2_classic_visited_level *level = &save->visited_levels[i];
    bool duplicate = false;
    for (size_t j = 0; j < i; j++)
      if (same_text(save->visited_levels[j].map, level->map)) duplicate = true;
    if (level->version != 1 || !valid_visited_map(level->map, save->map) ||
        duplicate || level->level.length == 0)
      qa_error("Invalid classic native visited level");
    validate_level_state(&level->state);
  }
  /* TS calls decodeCheckpointValue(save.server.cvars) for its throw. */
  qa_save_value *cvars = qa_decode_checkpoint_value(save->server.cvars.bytes,
                                                    save->server.cvars.length);
  qa_save_value_free(cvars);
  if (save->game.length == 0 || save->level.length == 0)
    qa_error("Classic native save requires both original files");
}

/* --- provider checkpoint encode/decode --- */
static qa_save_value *build_save(const qa_q2_classic_original_save *save) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  qa_save_value *module = qa_save_value_object();
  if (module == NULL ||
      !set_own(module, "id", qa_save_value_string(save->module.id != NULL ? save->module.id : "")) ||
      !set_own(module, "artifactPath", qa_save_value_string(
          save->module.artifact_path != NULL ? save->module.artifact_path : "")) ||
      !set_own(module, "digest", qa_save_value_string(
          save->module.digest != NULL ? save->module.digest : "")) ||
      !set_own(module, "revision", qa_save_value_string(
          save->module.revision != NULL ? save->module.revision : "")) ||
      !set_own(obj, "module", module))
    goto fail;
  if (!set_own(obj, "map", qa_save_value_string(save->map != NULL ? save->map : "")))
    goto fail;
  qa_save_value *api = qa_save_value_object();
  if (api == NULL ||
      !set_own(api, "kind", qa_save_value_string("q2-classic-game")) ||
      !set_own(api, "version", qa_save_value_number(3)) ||
      !set_own(obj, "api", api))
    goto fail;
  if (!set_own(obj, "abi", qa_save_value_string("windows-i386")) ||
      !set_own(obj, "autosave", qa_save_value_boolean(save->autosave)))
    goto fail;
  qa_save_value *server = qa_save_value_object();
  if (server == NULL || !build_level_state_into(server, &save->server.state) ||
      !set_own(server, "cvars", qa_save_value_bytes(save->server.cvars.bytes,
                                                    save->server.cvars.length)) ||
      !set_own(obj, "server", server))
    goto fail;
  if (!set_own(obj, "game", qa_save_value_bytes(save->game.bytes, save->game.length)) ||
      !set_own(obj, "level", qa_save_value_bytes(save->level.bytes, save->level.length)))
    goto fail;
  qa_save_value *visited = qa_save_value_array();
  if (visited == NULL) goto fail;
  for (size_t i = 0; i < save->visited_level_count; i++) {
    const qa_q2_classic_visited_level *level = &save->visited_levels[i];
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL ||
        !set_own(entry, "version", qa_save_value_number(1)) ||
        !build_level_state_into(entry, &level->state) ||
        !set_own(entry, "map", qa_save_value_string(level->map != NULL ? level->map : "")) ||
        !set_own(entry, "level", qa_save_value_bytes(level->level.bytes,
                                                     level->level.length)) ||
        !push_own(visited, entry)) {
      qa_save_value_free(entry);
      qa_save_value_free(visited);
      goto fail;
    }
  }
  if (!set_own(obj, "visitedLevels", visited)) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}
bool qa_encode_q2_classic_original_save(const qa_q2_classic_original_save *save,
                                        qa_provider_checkpoint *out) {
  qa_q2_classic_original_identity identity = {save->module, save->map};
  validate(save, identity);
  qa_save_value *value = build_save(save);
  if (value == NULL) return false;
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  if (bytes == NULL) return false;
  out->provider = save->module.id;
  out->schema = "q2:classic-native-original";
  out->version = 1;
  out->bytes.bytes = bytes;
  out->bytes.length = length;
  return true;
}
bool qa_decode_q2_classic_original_save(const qa_provider_checkpoint *record,
                                        qa_q2_classic_original_identity expected,
                                        qa_q2_classic_original_save *out) {
  memset(out, 0, sizeof *out);
  if (!same_text(record->schema, "q2:classic-native-original") ||
      record->version != 1 || !same_text(record->provider, expected.module.id))
    qa_error("Unsupported classic native source save provider");
  qa_save_value *root = qa_decode_checkpoint_value(record->bytes.bytes,
                                                   record->bytes.length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2.classic-native-original")) {
    qa_save_value_free(root);
    return false;
  }
  out->root = root;
  qa_save_reader api = field(&reader, "api");
  qa_save_reader kind = field(&api, "kind");
  (void)qa_save_reader_literal_string(&kind, "q2-classic-game");
  qa_save_reader_destroy(&kind);
  qa_save_reader version = field(&api, "version");
  (void)qa_save_reader_literal_number(&version, 3);
  qa_save_reader_destroy(&version);
  qa_save_reader_destroy(&api);
  qa_save_reader abi = field(&reader, "abi");
  (void)qa_save_reader_literal_string(&abi, "windows-i386");
  qa_save_reader_destroy(&abi);
  qa_save_reader server = field(&reader, "server");
  read_level_state(&server, &out->server.state);
  qa_save_reader cvars = field(&server, "cvars");
  out->server.cvars.bytes = qa_save_reader_bytes(&cvars, &out->server.cvars.length);
  qa_save_reader_destroy(&cvars);
  qa_save_reader_destroy(&server);
  qa_save_reader visits = field(&reader, "visitedLevels");
  if (!qa_save_reader_is_undefined(&visits)) {
    out->visited_level_count = qa_save_reader_list_count(&visits);
    out->visited_levels = (qa_q2_classic_visited_level *)calloc(
        out->visited_level_count != 0 ? out->visited_level_count : 1,
        sizeof *out->visited_levels);
    if (out->visited_levels == NULL) qa_save_reader_fail(&visits, "out of memory");
    for (size_t i = 0; i < out->visited_level_count; i++) {
      qa_save_reader entry = item(&visits, i);
      qa_save_reader entry_version = field(&entry, "version");
      (void)qa_save_reader_literal_number(&entry_version, 1);
      qa_save_reader_destroy(&entry_version);
      out->visited_levels[i].version = 1;
      read_level_state(&entry, &out->visited_levels[i].state);
      qa_save_reader map = field(&entry, "map");
      out->visited_levels[i].map = qa_save_reader_string(&map);
      qa_save_reader_destroy(&map);
      qa_save_reader level = field(&entry, "level");
      out->visited_levels[i].level.bytes =
          qa_save_reader_bytes(&level, &out->visited_levels[i].level.length);
      qa_save_reader_destroy(&level);
      qa_save_reader_destroy(&entry);
    }
  }
  qa_save_reader_destroy(&visits);
  qa_save_reader module = field(&reader, "module");
  out->module = qa_read_module(&module);
  qa_save_reader_destroy(&module);
  qa_save_reader map = field(&reader, "map");
  out->map = qa_save_reader_string(&map);
  qa_save_reader_destroy(&map);
  qa_save_reader autosave = field(&reader, "autosave");
  out->autosave = qa_save_reader_boolean(&autosave);
  qa_save_reader_destroy(&autosave);
  qa_save_reader game = field(&reader, "game");
  out->game.bytes = qa_save_reader_bytes(&game, &out->game.length);
  qa_save_reader_destroy(&game);
  qa_save_reader level = field(&reader, "level");
  out->level.bytes = qa_save_reader_bytes(&level, &out->level.length);
  qa_save_reader_destroy(&level);
  qa_save_reader_destroy(&reader);
  validate(out, expected);
  return true;
}
static void level_state_free(qa_q2_classic_level_state *state) {
  free(state->configstrings);
  free(state->portals);
  state->configstrings = NULL;
  state->portals = NULL;
}
void qa_q2_classic_original_save_free(qa_q2_classic_original_save *save) {
  if (save == NULL) return;
  level_state_free(&save->server.state);
  for (size_t i = 0; i < save->visited_level_count; i++) {
    level_state_free(&save->visited_levels[i].state);
    if (save->root == NULL) free((void *)save->visited_levels[i].level.bytes);
  }
  free(save->visited_levels);
  if (save->root == NULL) {
    free((void *)save->server.cvars.bytes);
    free((void *)save->game.bytes);
    free((void *)save->level.bytes);
  }
  qa_save_value_free(save->root);
  memset(save, 0, sizeof *save);
}

/* --- ClassicOriginalSaveFiles overlay --- */
enum operation_kind {
  OP_CAPTURE, OP_RESTORE, OP_CAPTURE_LEVEL, OP_RESTORE_LEVEL
};
typedef struct file_entry {
  char *path;
  uint8_t *bytes;
  size_t length;
  size_t capacity;
  bool read;
  int open_handles;
} file_entry;
typedef struct save_handle save_handle;
struct save_handle {
  qa_q2_classic_file file;
  qa_classic_original_save_files *owner;
  size_t entry_index;
  uint64_t generation;
  bool closed;
  bool can_read;
  bool can_write;
  save_handle *next_all;
};
struct qa_classic_original_save_files {
  qa_q2_classic_open_file_fn fallback;
  void *fallback_ctx;
  bool active;
  enum operation_kind kind;
  uint64_t generation;
  file_entry *entries;
  size_t entry_count;
  size_t entry_capacity;
  save_handle *all_handles;
};
static const char k_root[] = "__qts_original_save/";
static const char k_game_path[] = "__qts_original_save/game.ssv";
static const char k_level_path[] = "__qts_original_save/level.sav";

qa_classic_original_save_files *qa_classic_original_save_files_create(
    qa_q2_classic_open_file_fn fallback, void *fallback_ctx) {
  qa_classic_original_save_files *files =
      (qa_classic_original_save_files *)calloc(1, sizeof *files);
  if (files == NULL) return NULL;
  files->fallback = fallback;
  files->fallback_ctx = fallback_ctx;
  return files;
}
static void entries_clear(qa_classic_original_save_files *files) {
  for (size_t i = 0; i < files->entry_count; i++) {
    free(files->entries[i].path);
    free(files->entries[i].bytes);
  }
  free(files->entries);
  files->entries = NULL;
  files->entry_count = 0;
  files->entry_capacity = 0;
}
void qa_classic_original_save_files_destroy(qa_classic_original_save_files *files) {
  if (files == NULL) return;
  entries_clear(files);
  save_handle *handle = files->all_handles;
  while (handle != NULL) {
    save_handle *next = handle->next_all;
    free(handle);
    handle = next;
  }
  free(files);
}
static file_entry *find_entry(qa_classic_original_save_files *files,
                              const char *path, size_t *out_index) {
  for (size_t i = 0; i < files->entry_count; i++) {
    if (strcmp(files->entries[i].path, path) == 0) {
      if (out_index != NULL) *out_index = i;
      return &files->entries[i];
    }
  }
  return NULL;
}
static bool add_entry(qa_classic_original_save_files *files, const char *path,
                      const uint8_t *bytes, size_t length, size_t *out_index) {
  if (files->entry_count == files->entry_capacity) {
    size_t capacity = files->entry_capacity == 0 ? 4 : files->entry_capacity * 2;
    file_entry *entries = (file_entry *)realloc(files->entries,
                                                capacity * sizeof *entries);
    if (entries == NULL) return false;
    files->entries = entries;
    files->entry_capacity = capacity;
  }
  file_entry *entry = &files->entries[files->entry_count];
  memset(entry, 0, sizeof *entry);
  entry->path = dup_text(path);
  entry->bytes = dup_bytes(bytes, length);
  if (entry->path == NULL || entry->bytes == NULL) {
    free(entry->path);
    free(entry->bytes);
    return false;
  }
  entry->length = length;
  entry->capacity = length;
  if (out_index != NULL) *out_index = files->entry_count;
  files->entry_count++;
  return true;
}
static void handle_live(const save_handle *handle) {
  if (handle->closed || !handle->owner->active ||
      handle->owner->generation != handle->generation)
    qa_error("Native save handle is retired");
}
static void check_range(size_t offset, size_t length) {
  if (offset > 9007199254740991u || length > 9007199254740991u ||
      offset + length < offset || offset + length > 9007199254740991u)
    qa_range_error("Invalid native save file range");
}
static size_t handle_read(void *ctx, size_t offset, uint8_t *out, size_t length) {
  save_handle *handle = (save_handle *)ctx;
  handle_live(handle);
  if (!handle->can_read) qa_error("Native save file is not readable");
  check_range(offset, length);
  file_entry *entry = &handle->owner->entries[handle->entry_index];
  if (length > 0 && offset < entry->length) entry->read = true;
  size_t available = offset < entry->length ? entry->length - offset : 0;
  size_t count = length < available ? length : available;
  if (count > 0) memcpy(out, entry->bytes + offset, count);
  return count;
}
static bool entry_resize(file_entry *entry, size_t length) {
  if (length > entry->capacity) {
    uint8_t *bytes = (uint8_t *)realloc(entry->bytes, length);
    if (bytes == NULL) return false;
    entry->bytes = bytes;
    entry->capacity = length;
  }
  /* TS growth always copies into a fresh zero-filled Uint8Array, so
   * bytes beyond the old length read as zero even within capacity. */
  if (length > entry->length)
    memset(entry->bytes + entry->length, 0, length - entry->length);
  entry->length = length;
  return true;
}
static int64_t handle_write(void *ctx, size_t offset, const uint8_t *bytes,
                            size_t length) {
  save_handle *handle = (save_handle *)ctx;
  handle_live(handle);
  if (!handle->can_write) qa_error("Native save file is not writable");
  check_range(offset, length);
  file_entry *entry = &handle->owner->entries[handle->entry_index];
  if (offset + length > entry->length && !entry_resize(entry, offset + length))
    qa_error("out of memory");
  if (length > 0) memcpy(entry->bytes + offset, bytes, length);
  return (int64_t)length;
}
static int64_t handle_size(void *ctx) {
  save_handle *handle = (save_handle *)ctx;
  handle_live(handle);
  return (int64_t)handle->owner->entries[handle->entry_index].length;
}
static void handle_truncate(void *ctx, size_t length) {
  save_handle *handle = (save_handle *)ctx;
  handle_live(handle);
  if (!handle->can_write) qa_error("Native save file is not writable");
  check_range(length, 0);
  file_entry *entry = &handle->owner->entries[handle->entry_index];
  size_t kept = length < entry->length ? length : entry->length;
  if (!entry_resize(entry, length)) qa_error("out of memory");
  (void)kept;
}
static void handle_flush(void *ctx) {
  save_handle *handle = (save_handle *)ctx;
  handle_live(handle);
}
static void handle_close(void *ctx) {
  save_handle *handle = (save_handle *)ctx;
  if (handle->closed) return;
  handle->closed = true;
  if (handle->owner->active && handle->owner->generation == handle->generation)
    handle->owner->entries[handle->entry_index].open_handles--;
}
qa_q2_classic_file *qa_classic_original_save_files_open_file(
    qa_classic_original_save_files *files, const char *path, bool read, bool write,
    int creation) {
  char *normalized = dup_text(path);
  if (normalized == NULL) return NULL;
  for (char *p = normalized; *p != '\0'; p++)
    if (*p == '\\') *p = '/';
  char *stripped = normalized;
  while (strncmp(stripped, "./", 2) == 0) stripped += 2;
  if (strncmp(stripped, k_root, strlen(k_root)) != 0) {
    free(normalized);
    return files->fallback != NULL
        ? files->fallback(files->fallback_ctx, path, read, write, creation)
        : NULL;
  }
  bool level_only = files->active &&
      (files->kind == OP_CAPTURE_LEVEL || files->kind == OP_RESTORE_LEVEL);
  if (!files->active ||
      (strcmp(stripped, k_game_path) != 0 && strcmp(stripped, k_level_path) != 0) ||
      (level_only && strcmp(stripped, k_level_path) != 0)) {
    free(normalized);
    qa_error("Native save file is outside the active callback operation");
  }
  if (creation < 1 || creation > 5) {
    free(normalized);
    qa_range_error("Invalid native save file disposition");
  }
  if ((files->kind == OP_RESTORE || files->kind == OP_RESTORE_LEVEL) &&
      (write || creation != 3)) {
    free(normalized);
    qa_error("Original restore files are read-only");
  }
  if (!write && creation != 3) {
    free(normalized);
    return NULL;
  }
  size_t index = 0;
  file_entry *entry = find_entry(files, stripped, &index);
  if ((creation == 1 && entry != NULL) ||
      ((creation == 3 || creation == 5) && entry == NULL)) {
    free(normalized);
    return NULL;
  }
  if (entry == NULL) {
    if (!add_entry(files, stripped, NULL, 0, &index)) {
      free(normalized);
      return NULL;
    }
    entry = &files->entries[index];
  }
  if (creation == 2 || creation == 5) entry->length = 0;
  save_handle *handle = (save_handle *)calloc(1, sizeof *handle);
  if (handle == NULL) {
    free(normalized);
    return NULL;
  }
  handle->file.ctx = handle;
  handle->file.read = handle_read;
  handle->file.write = handle_write;
  handle->file.size = handle_size;
  handle->file.truncate = handle_truncate;
  handle->file.flush = handle_flush;
  handle->file.close = handle_close;
  handle->owner = files;
  handle->entry_index = index;
  handle->generation = files->generation;
  handle->can_read = read;
  handle->can_write = write;
  handle->next_all = files->all_handles;
  files->all_handles = handle;
  files->entries[index].open_handles++;
  free(normalized);
  return &handle->file;
}
typedef struct initial_bytes {
  bool is_game;
  const uint8_t *bytes;
  size_t length;
} initial_bytes;
static bool operation_begin(qa_classic_original_save_files *files,
                            enum operation_kind kind,
                            const initial_bytes *initial, size_t initial_count) {
  if (files->active) qa_error("Native save callbacks cannot overlap");
  files->kind = kind;
  files->generation++;
  files->active = true;
  for (size_t i = 0; i < initial_count; i++) {
    if (!add_entry(files, initial[i].is_game ? k_game_path : k_level_path,
                   initial[i].bytes, initial[i].length, NULL)) {
      entries_clear(files);
      files->active = false;
      return false;
    }
  }
  return true;
}
static void operation_end(qa_classic_original_save_files *files) {
  for (save_handle *handle = files->all_handles; handle != NULL;
       handle = handle->next_all)
    if (handle->generation == files->generation && !handle->closed)
      handle_close(handle);
  entries_clear(files);
  files->active = false;
}
static void assert_closed(qa_classic_original_save_files *files) {
  for (size_t i = 0; i < files->entry_count; i++)
    if (files->entries[i].open_handles != 0)
      qa_error("Native save callback retained an open original file");
}
static bool copy_level_state(const qa_q2_classic_level_state *source,
                             qa_q2_classic_level_state *out) {
  memset(out, 0, sizeof *out);
  out->configstring_count = source->configstring_count;
  out->configstrings = (qa_q2_classic_configstring_entry *)calloc(
      out->configstring_count != 0 ? out->configstring_count : 1,
      sizeof *out->configstrings);
  out->portal_count = source->portal_count;
  out->portals = (qa_q2_classic_portal_entry *)calloc(
      out->portal_count != 0 ? out->portal_count : 1, sizeof *out->portals);
  if (out->configstrings == NULL || out->portals == NULL) return false;
  if (out->configstring_count != 0)
    memcpy(out->configstrings, source->configstrings,
           out->configstring_count * sizeof *out->configstrings);
  if (out->portal_count != 0)
    memcpy(out->portals, source->portals,
           out->portal_count * sizeof *out->portals);
  return true;
}
bool qa_classic_original_save_files_capture(
    qa_classic_original_save_files *files, qa_q2_classic_original_identity identity,
    qa_q2_classic_server_state_fn server_state, void *server_ctx,
    qa_q2_classic_write_fn write, void *write_ctx, bool autosave,
    qa_q2_classic_original_save *out) {
  memset(out, 0, sizeof *out);
  if (!operation_begin(files, OP_CAPTURE, NULL, 0)) return false;
  write(write_ctx, k_game_path, k_level_path, autosave);
  assert_closed(files);
  file_entry *game = find_entry(files, k_game_path, NULL);
  file_entry *level = find_entry(files, k_level_path, NULL);
  if (game == NULL || level == NULL)
    qa_error("Native save callbacks did not write both original files");
  qa_q2_classic_original_server_state server;
  memset(&server, 0, sizeof server);
  server_state(server_ctx, &server);
  uint8_t *game_copy = dup_bytes(game->bytes, game->length);
  size_t game_length = game->length;
  uint8_t *level_copy = dup_bytes(level->bytes, level->length);
  size_t level_length = level->length;
  operation_end(files);
  if (game_copy == NULL || level_copy == NULL ||
      !copy_level_state(&server.state, &out->server.state)) {
    free(game_copy);
    free(level_copy);
    return false;
  }
  out->module = identity.module;
  out->map = identity.map;
  out->autosave = autosave;
  out->server.cvars.bytes = dup_bytes(server.cvars.bytes, server.cvars.length);
  out->server.cvars.length = server.cvars.length;
  out->game.bytes = game_copy;
  out->game.length = game_length;
  out->level.bytes = level_copy;
  out->level.length = level_length;
  if (out->server.cvars.bytes == NULL) return false;
  validate(out, identity);
  return true;
}
void qa_classic_original_save_files_restore(
    qa_classic_original_save_files *files, const qa_q2_classic_original_save *save,
    qa_q2_classic_original_identity expected, qa_q2_classic_read_fn read,
    void *read_ctx) {
  validate(save, expected);
  const initial_bytes initial[] = {
      {true, save->game.bytes, save->game.length},
      {false, save->level.bytes, save->level.length}};
  if (!operation_begin(files, OP_RESTORE, initial, 2))
    qa_error("out of memory");
  read(read_ctx, k_game_path, k_level_path);
  assert_closed(files);
  bool consumed = true;
  for (size_t i = 0; i < files->entry_count; i++)
    if (!files->entries[i].read) consumed = false;
  operation_end(files);
  if (!consumed)
    qa_error("Native restore callbacks did not consume both original files");
}
void qa_classic_original_save_files_restore_loading(
    qa_classic_original_save_files *files, const qa_q2_classic_original_save *save,
    qa_q2_classic_original_identity expected, qa_q2_classic_read_fn read,
    void *read_ctx) {
  qa_classic_original_save_files_restore(files, save, expected, read, read_ctx);
}
bool qa_classic_original_save_files_capture_travel_level(
    qa_classic_original_save_files *files, qa_q2_classic_level_write_fn write,
    void *write_ctx, uint8_t **out_bytes, size_t *out_length) {
  if (!operation_begin(files, OP_CAPTURE_LEVEL, NULL, 0)) return false;
  write(write_ctx, k_level_path);
  assert_closed(files);
  file_entry *level = find_entry(files, k_level_path, NULL);
  if (level == NULL || level->length == 0)
    qa_error("Native travel callback did not write a level file");
  *out_bytes = dup_bytes(level->bytes, level->length);
  *out_length = level->length;
  operation_end(files);
  return *out_bytes != NULL;
}
bool qa_classic_original_save_files_capture_travel_level_loading(
    qa_classic_original_save_files *files, qa_q2_classic_level_write_fn write,
    void *write_ctx, uint8_t **out_bytes, size_t *out_length) {
  return qa_classic_original_save_files_capture_travel_level(files, write, write_ctx,
                                                             out_bytes, out_length);
}
void *qa_classic_original_save_files_with_travel_level(
    qa_classic_original_save_files *files, const uint8_t *bytes, size_t length,
    qa_q2_classic_level_read_fn read, void *read_ctx) {
  if (length == 0) qa_error("Native travel requires original level bytes");
  const initial_bytes initial[] = {{false, bytes, length}};
  if (!operation_begin(files, OP_RESTORE_LEVEL, initial, 1))
    qa_error("out of memory");
  void *result = read(read_ctx, k_level_path);
  assert_closed(files);
  file_entry *level = find_entry(files, k_level_path, NULL);
  bool consumed = level != NULL && level->read;
  operation_end(files);
  if (!consumed)
    qa_error("Native travel callback did not consume the level file");
  return result;
}
void *qa_classic_original_save_files_with_travel_level_loading(
    qa_classic_original_save_files *files, const uint8_t *bytes, size_t length,
    qa_q2_classic_level_read_fn read, void *read_ctx) {
  return qa_classic_original_save_files_with_travel_level(files, bytes, length,
                                                          read, read_ctx);
}
