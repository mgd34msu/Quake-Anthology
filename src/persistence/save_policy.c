/* Port of src/persistence/save-policy.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/persistence/save_policy.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/support.h"

const char *qa_save_unavailable(const qa_save_eligibility *state,
                                qa_save_purpose purpose) {
  if (!state->active) return "No active world to save.";
  if (purpose == QA_SAVE_PURPOSE_TRANSITION)
    return state->authority == QA_SAVE_AUTHORITY_REMOTE
               ? "Only the authoritative world can retain transition state."
               : NULL;
  if (state->authority != QA_SAVE_AUTHORITY_OFFLINE)
    return "Save/load unavailable while hosting a network game.";
  if (state->family != QA_GAME_FAMILY_Q3 &&
      state->mode == QA_SAVE_MODE_DEATHMATCH && !state->match_save)
    return "Cannot save a deathmatch game.";
  if (state->family != QA_GAME_FAMILY_Q3 && state->intermission &&
      !state->match_save)
    return "Cannot save during intermission.";
  /* Server-administration saves persist world state even with no players in it. */
  if (state->player_health_count == 0 && !state->match_save)
    return "No active player to save.";
  if (state->family != QA_GAME_FAMILY_Q3 && !state->match_save) {
    for (size_t i = 0; i < state->player_health_count; i++)
      if (!isfinite(state->player_health[i]) || state->player_health[i] <= 0)
        return "Cannot save with a dead player.";
  }
  return NULL;
}

/* --- node:path (POSIX) resolve/relative/extname for byte strings --- */

static char *dup_range(const char *start, size_t length) {
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, start, length);
  out[length] = '\0';
  return out;
}

/* Normalize an absolute path: collapse "/", "." and ".." segments. */
static char *normalize_absolute(const char *path) {
  size_t length = strlen(path);
  char *work = dup_range(path, length);
  if (work == NULL) return NULL;
  /* Segment stack over the mutable copy. */
  char **segments = malloc((length / 2 + 2) * sizeof *segments);
  if (segments == NULL) { free(work); return NULL; }
  size_t count = 0;
  char *cursor = work;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    if (*cursor == '/') { *cursor = '\0'; cursor++; }
    if (strcmp(start, ".") == 0) continue;
    if (strcmp(start, "..") == 0) { if (count > 0) count--; continue; }
    segments[count++] = start;
  }
  size_t total = 1;
  for (size_t i = 0; i < count; i++) total += strlen(segments[i]) + 1;
  char *out = malloc(total + 1);
  if (out == NULL) { free(segments); free(work); return NULL; }
  char *at = out;
  if (count == 0) {
    *at++ = '/';
  } else {
    for (size_t i = 0; i < count; i++) {
      *at++ = '/';
      size_t n = strlen(segments[i]);
      memcpy(at, segments[i], n);
      at += n;
    }
  }
  *at = '\0';
  free(segments);
  free(work);
  return out;
}

/* node:path resolve(...): rightmost absolute segment wins, else the
 * result is anchored at the current working directory. */
static char *path_resolve(const char *directory, const char *name) {
  const char *joined_from = NULL;
  char *joined = NULL;
  if (name != NULL && name[0] == '/') {
    joined_from = name;
  } else if (directory != NULL && directory[0] == '/') {
    size_t dl = strlen(directory), nl = name == NULL ? 0 : strlen(name);
    joined = malloc(dl + 1 + nl + 1);
    if (joined == NULL) return NULL;
    memcpy(joined, directory, dl);
    joined[dl] = '/';
    if (nl > 0) memcpy(joined + dl + 1, name, nl);
    joined[dl + 1 + nl] = '\0';
    joined_from = joined;
  } else {
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) return NULL;
    size_t cl = strlen(cwd);
    size_t dl = directory == NULL ? 0 : strlen(directory);
    size_t nl = name == NULL ? 0 : strlen(name);
    joined = malloc(cl + 1 + dl + 1 + nl + 1);
    if (joined == NULL) return NULL;
    char *at = joined;
    memcpy(at, cwd, cl); at += cl;
    if (dl > 0) { *at++ = '/'; memcpy(at, directory, dl); at += dl; }
    if (nl > 0) { *at++ = '/'; memcpy(at, name, nl); at += nl; }
    *at = '\0';
    joined_from = joined;
  }
  char *out = normalize_absolute(joined_from);
  free(joined);
  return out;
}

/* node:path extname: from the last "." in the final segment, where a
 * leading-dot basename (".sav") has no extension. */
static bool has_extension(const char *name) {
  const char *base = strrchr(name, '/');
  base = base == NULL ? name : base + 1;
  const char *dot = strrchr(base, '.');
  if (dot == NULL || dot == base) return false;
  /* ".." and "." basenames: node returns "" for "." and ".." handled
   * by the dot==base+? rule; "..sav" style names keep an extension. */
  if (dot == base + 1 && base[0] == '.' && dot[1] == '\0') return false;
  return true;
}

char *qa_save_command_path(const char *directory, const char *name) {
  if (name[0] == '\0') qa_range_error("Enter a save name or file path");
  if (has_extension(name)) return path_resolve(directory, name);
  size_t nl = strlen(name);
  char *with_ext = malloc(nl + sizeof ".sav");
  if (with_ext == NULL) return NULL;
  memcpy(with_ext, name, nl);
  memcpy(with_ext + nl, ".sav", sizeof ".sav");
  char *out = path_resolve(directory, with_ext);
  free(with_ext);
  return out;
}

/* TS containedFileParts (platform/files/contained.ts): splits on "/"
 * and rejects empty/"."/".." parts, "\\", NUL and ":". Returns a
 * malloc'd copy split in place; *out_parts indexes into it. */
static char *contained_file_parts(const char *name, char ***out_parts,
                                  size_t *out_count) {
  size_t length = strlen(name);
  bool invalid = length == 0 || strchr(name, '\\') != NULL ||
                 strchr(name, ':') != NULL;
  char *work = dup_range(name, length);
  if (work == NULL) return NULL;
  size_t capacity = length / 2 + 2;
  char **parts = malloc(capacity * sizeof *parts);
  if (parts == NULL) { free(work); return NULL; }
  size_t count = 0;
  char *cursor = work;
  for (;;) {
    char *slash = strchr(cursor, '/');
    if (slash != NULL) *slash = '\0';
    if (cursor[0] == '\0' || strcmp(cursor, ".") == 0 ||
        strcmp(cursor, "..") == 0)
      invalid = true;
    parts[count++] = cursor;
    if (slash == NULL) break;
    cursor = slash + 1;
  }
  if (invalid) {
    free(parts);
    free(work);
    qa_range_error(
        "File path must be relative and stay inside its storage directory");
  }
  *out_parts = parts;
  *out_count = count;
  return work;
}

static bool ascii_equal_ignore_case(const char *left, const char *right,
                                    size_t length) {
  if (strlen(right) != length) return false;
  for (size_t i = 0; i < length; i++)
    if (tolower((unsigned char)left[i]) != tolower((unsigned char)right[i]))
      return false;
  return true;
}

static bool ends_with_sav_ignore_case(const char *name) {
  size_t length = strlen(name);
  return length >= 4 && ascii_equal_ignore_case(name + length - 4, ".sav", 4);
}

char *qa_contained_save_name(const char *directory, const char *path) {
  char *root = path_resolve(directory, NULL);
  char *full = path_resolve(path, NULL);
  if (root == NULL || full == NULL) { free(root); free(full); return NULL; }
  /* node:path relative(root, full) for two absolute paths. */
  char *name = NULL;
  size_t root_length = strlen(root);
  if (strcmp(root, full) == 0) {
    name = dup_range("", 0);
  } else if (strncmp(full, root, root_length) == 0 &&
             (root_length == 1 || full[root_length] == '/')) {
    const char *rest = full + root_length;
    if (*rest == '/') rest++;
    name = dup_range(rest, strlen(rest));
  }
  if (name == NULL) {
    /* Relative form would be absolute-ish or start with "..". */
    char *message_root = root;
    free(full);
    qa_range_error("Save path is outside the save directory: %s",
                   message_root);
  }
  free(full);
  char **parts = NULL;
  size_t part_count = 0;
  char *work = contained_file_parts(name, &parts, &part_count);
  if (work == NULL) { free(root); free(name); return NULL; }
  for (size_t i = 0; i < part_count; i++) {
    size_t part_length = strlen(parts[i]);
    size_t bare = part_length;
    if (bare >= 4 && ascii_equal_ignore_case(parts[i] + bare - 4, ".sav", 4))
      bare -= 4;
    if (ascii_equal_ignore_case(parts[i], "current", bare) && bare == 7) {
      free(parts); free(work); free(root); free(name);
      qa_range_error("The current slot is reserved for transition state");
    }
  }
  free(parts);
  free(work);
  free(root);
  if (!ends_with_sav_ignore_case(name)) {
    free(name);
    qa_range_error("Save path must end in .sav");
  }
  return name;
}

/* mkdir -p for the save directory itself (TS mkdir recursive). */
static bool mkdir_recursive(const char *directory) {
  char *absolute = path_resolve(directory, NULL);
  if (absolute == NULL) return false;
  bool ok = true;
  for (char *at = absolute + 1;; at++) {
    if (*at == '/' || *at == '\0') {
      char saved = *at;
      *at = '\0';
      if (mkdir(absolute, 0777) != 0 && errno != EEXIST) ok = false;
      *at = saved;
      if (saved == '\0' || !ok) break;
    }
  }
  free(absolute);
  return ok;
}

bool qa_write_contained_save(const char *directory, const char *path,
                             const uint8_t *bytes, size_t length) {
  char *name = qa_contained_save_name(directory, path);
  if (name == NULL) return false;
  if (!mkdir_recursive(directory)) { free(name); return false; }
  char **parts = NULL;
  size_t part_count = 0;
  char *work = contained_file_parts(name, &parts, &part_count);
  free(name);
  if (work == NULL) return false;
  /* TS paths here are already validated; a filename always exists. */
  char *root = path_resolve(directory, NULL);
  if (root == NULL) { free(parts); free(work); return false; }
  int descriptor = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  free(root);
  if (descriptor < 0) { free(parts); free(work); return false; }
  bool ok = true;
  for (size_t i = 0; i + 1 < part_count && ok; i++) {
    if (mkdirat(descriptor, parts[i], 0777) != 0 && errno != EEXIST) ok = false;
    if (!ok) break;
    int next = openat(descriptor, parts[i],
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (next < 0) { ok = false; break; }
    close(descriptor);
    descriptor = next;
  }
  const char *leaf = parts[part_count - 1];
  char temporary[1024];
  static unsigned long counter = 0;
  int written = snprintf(temporary, sizeof temporary, ".%s.%ld-%lu.tmp", leaf,
                         (long)getpid(), ++counter);
  if (written < 0 || (size_t)written >= sizeof temporary) ok = false;
  int file = -1;
  if (ok) {
    file = openat(descriptor, temporary,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (file < 0) ok = false;
  }
  if (ok) {
    size_t position = 0;
    while (position < length) {
      ssize_t count = write(file, bytes + position, length - position);
      if (count <= 0) { ok = false; break; } /* TS: "Save write made no progress" */
      position += (size_t)count;
    }
    if (ok && fsync(file) != 0) ok = false;
    close(file);
    file = -1;
  }
  if (ok && renameat(descriptor, temporary, descriptor, leaf) != 0) ok = false;
  if (ok && fsync(descriptor) != 0) ok = false;
  unlinkat(descriptor, temporary, 0); /* rm force: already renamed on success */
  close(descriptor);
  free(parts);
  free(work);
  return ok;
}

/* TS assertLoadPathAllowed (added for startup.ts routeCommand). */
static bool load_path_inside(const char *root_input, const char *full) {
  char *root = path_resolve(root_input, NULL);
  if (root == NULL) qa_range_error("Out of memory");
  size_t root_length = strlen(root);
  bool inside = strcmp(root, full) == 0 || (strncmp(full, root, root_length) == 0 &&
      (root_length == 1 || full[root_length] == '/'));
  free(root);
  return inside;
}
void qa_assert_load_path_allowed(const char *directory, const char *path, const qa_command_context *source,
                                 bool allow_any_path, const char *const *trusted_directories, size_t trusted_count) {
  if (allow_any_path) return;
  char *full = path_resolve(path, NULL);
  if (full == NULL) qa_range_error("Out of memory");
  bool inside = load_path_inside(directory, full);
  for (size_t i = 0; !inside && i < trusted_count; i++) inside = load_path_inside(trusted_directories[i], full);
  if (inside || source == NULL || (source->producer == NULL && (source->origin.kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE
      || source->origin.kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE || source->origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT))) {
    free(full);
    return;
  }
  char message[4096];
  snprintf(message, sizeof message, "%s", full);
  free(full);
  qa_range_error("load of a path outside the save directory is only accepted from the local console; start with "
      "--allow-load-paths to allow it from scripts, game modules and remote clients: %s", message);
}
