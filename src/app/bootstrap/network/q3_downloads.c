/* Port of src/app/bootstrap/network/q3-downloads.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_downloads.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/content/archive.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/pure.h"
typedef struct member_set { char **paths; size_t count; } member_set;
struct qa_app_q3_packages {
  qa_q3_mounted_pak *packs; size_t pack_count;
  member_set *members; /* parallel to packs */
  char **games, **basenames; /* owned, parallel to packs */
  qa_q3_content_references *references;
  qa_resource_id *processed; size_t processed_count, processed_cap;
};
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
static char *lower_copy(const char *s) {
  char *out = copy_text(s);
  if (out) for (char *p = out; *p; p++) *p = tolower((unsigned char)*p);
  return out;
}
static double one_random(void *ctx) { (void)ctx; return 1; }
qa_app_q3_packages *qa_app_q3_packages_open(const qa_app_q3_package_mount *mounts,
    size_t mount_count, double checksum_feed) {
  qa_app_q3_packages *packages = calloc(1, sizeof *packages);
  if (!packages) return NULL;
  size_t capacity = 0;
  for (size_t i = 0; i < mount_count; i++) {
    const qa_app_q3_package_mount *candidate = &mounts[i];
    if (!candidate->is_archive || candidate->mount.format != QA_ARCHIVE_FORMAT_PK3) continue;
    if (strcmp(candidate->product_family, "q3") != 0) continue;
    qa_archive_handle *archive = qa_open_archive(candidate->mount.archive_path,
        true, candidate->mount.format);
    const char *slash = strrchr(candidate->content_directory, '/');
    const char *game = slash ? slash + 1 : candidate->content_directory;
    if (game[0] == '\0') {
      qa_archive_destroy(archive);
      qa_range_error("Q3 archive has no game directory");
    }
    const char *file = strrchr(candidate->mount.archive_path, '/');
    file = file ? file + 1 : candidate->mount.archive_path;
    size_t base_length = strlen(file);
    if (base_length >= 4 && strcasecmp(file + base_length - 4, ".pk3") == 0) base_length -= 4;
    char *basename = malloc(base_length + 1);
    char *game_copy = copy_text(game);
    if (!basename || !game_copy) { free(basename); free(game_copy); qa_archive_destroy(archive); qa_range_error("out of memory"); }
    memcpy(basename, file, base_length); basename[base_length] = '\0';
    if (packages->pack_count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      packages->packs = realloc(packages->packs, capacity * sizeof *packages->packs);
      packages->members = realloc(packages->members, capacity * sizeof *packages->members);
      packages->games = realloc(packages->games, capacity * sizeof *packages->games);
      packages->basenames = realloc(packages->basenames, capacity * sizeof *packages->basenames);
      if (!packages->packs || !packages->members || !packages->games || !packages->basenames)
        qa_range_error("out of memory");
    }
    size_t slot = packages->pack_count++;
    packages->games[slot] = game_copy; packages->basenames[slot] = basename;
    packages->packs[slot] = qa_q3_register_pak(&candidate->mount, archive,
        game_copy, basename, (int64_t)checksum_feed);
    member_set *set = &packages->members[slot];
    set->paths = NULL; set->count = 0;
    size_t member_cap = 0;
    for (size_t e = 0; e < archive->entry_count; e++) {
      if (archive->entries[e].is_directory) continue;
      if (set->count == member_cap) {
        member_cap = member_cap ? member_cap * 2 : 16;
        set->paths = realloc(set->paths, member_cap * sizeof *set->paths);
        if (!set->paths) qa_range_error("out of memory");
      }
      set->paths[set->count++] = lower_copy(archive->entries[e].path);
    }
    qa_archive_destroy(archive);
  }
  if (!qa_q3_content_references_init(&packages->references, packages->packs,
          packages->pack_count, (int64_t)checksum_feed, one_random, NULL)) {
    qa_app_q3_packages_destroy(packages);
    return NULL;
  }
  return packages;
}
void qa_app_q3_packages_destroy(qa_app_q3_packages *packages) {
  if (!packages) return;
  qa_q3_content_references_destroy(packages->references);
  for (size_t i = 0; i < packages->pack_count; i++) {
    for (size_t j = 0; j < packages->members[i].count; j++) free(packages->members[i].paths[j]);
    free(packages->members[i].paths);
    free(packages->games[i]); free(packages->basenames[i]);
  }
  free(packages->packs); free(packages->members);
  free(packages->games); free(packages->basenames);
  free(packages->processed); free(packages);
}
const qa_q3_mounted_pak *qa_app_q3_packages_packs(const qa_app_q3_packages *packages, size_t *out_count) {
  *out_count = packages->pack_count;
  return packages->packs;
}
qa_q3_content_references *qa_app_q3_packages_references(qa_app_q3_packages *packages) {
  return packages->references;
}
void qa_app_q3_packages_collect(qa_app_q3_packages *packages,
    const qa_resolved_resource_reference *const *references, size_t count,
    bool (*is_q3)(void *ctx, const qa_resolved_resource_reference *reference), void *ctx) {
  for (size_t i = 0; i < count; i++) {
    const qa_resolved_resource_reference *reference = references[i];
    bool seen = false;
    for (size_t j = 0; j < packages->processed_count; j++)
      if (strcmp(packages->processed[j], reference->id) == 0) seen = true;
    if (seen || !is_q3(ctx, reference)) continue;
    qa_q3_content_references_opened(packages->references, reference);
    if (packages->processed_count == packages->processed_cap) {
      packages->processed_cap = packages->processed_cap ? packages->processed_cap * 2 : 16;
      packages->processed = realloc(packages->processed,
          packages->processed_cap * sizeof *packages->processed);
      if (!packages->processed) qa_range_error("out of memory");
    }
    packages->processed[packages->processed_count++] = reference->id;
  }
}
bool qa_app_q3_packages_pure_checksum(const qa_app_q3_packages *packages,
    const char *path, double *out) {
  char *lowered = lower_copy(path);
  if (!lowered) return false;
  for (size_t i = 0; i < packages->pack_count; i++)
    for (size_t j = 0; j < packages->members[i].count; j++)
      if (strcmp(packages->members[i].paths[j], lowered) == 0) {
        *out = (double)packages->packs[i].pack.pure_checksum;
        free(lowered);
        return true;
      }
  free(lowered);
  return false;
}
typedef struct download_file {
  int fd; int64_t size, position; bool closed;
  struct stat original;
} download_file;
static void assert_unchanged(download_file *file) {
  struct stat current;
  if (fstat(file->fd, &current) != 0
      || current.st_size != file->original.st_size
      || current.st_mtim.tv_sec != file->original.st_mtim.tv_sec
      || current.st_mtim.tv_nsec != file->original.st_mtim.tv_nsec
      || current.st_ctim.tv_sec != file->original.st_ctim.tv_sec
      || current.st_ctim.tv_nsec != file->original.st_ctim.tv_nsec)
    qa_range_error("Download archive changed after opening");
}
static int64_t download_read(void *ctx, uint8_t *target, size_t capacity) {
  download_file *file = ctx;
  if (file->closed) qa_range_error("Q3 download is closed");
  assert_unchanged(file);
  int64_t remaining = file->size - file->position;
  size_t want = capacity < (size_t)remaining ? capacity : (size_t)remaining;
  ssize_t count = pread(file->fd, target, want, file->position);
  if (count < 0) qa_range_error("Truncated mounted download");
  file->position += count;
  return count;
}
static void download_close(void *ctx) {
  download_file *file = ctx;
  if (!file->closed) { file->closed = true; close(file->fd); }
  free(file);
}
bool qa_app_q3_packages_open_download(qa_app_q3_packages *packages,
    const char *name, qa_q3_download_read_file *out) {
  qa_q3_check_download_name(name);
  const qa_q3_mounted_pak *mounted = NULL;
  for (size_t i = 0; i < packages->pack_count; i++) {
    const qa_q3_mounted_pak *pack = &packages->packs[i];
    size_t n = strlen(pack->pack.game) + strlen(pack->pack.basename) + 6;
    char *candidate = malloc(n);
    if (!candidate) qa_range_error("out of memory");
    snprintf(candidate, n, "%s/%s.pk3", pack->pack.game, pack->pack.basename);
    bool match = strcasecmp(candidate, name) == 0;
    free(candidate);
    if (match) { mounted = pack; break; }
  }
  if (!mounted) return false;
  int fd = open(mounted->mount.archive_path, O_RDONLY);
  if (fd < 0) qa_range_error("out of memory");
  struct stat original;
  if (fstat(fd, &original) != 0 || !S_ISREG(original.st_mode)
      || original.st_size <= 0 || original.st_size > 0x7fffffff) {
    close(fd);
    return false;
  }
  /* The TS hashes through this descriptor between unchanged checks;
   * the landed digest helper hashes by path with the fd checks
   * bracketing it. */
  download_file probe = {fd, original.st_size, 0, false, original};
  char *digest = qa_digest_file(mounted->mount.archive_path);
  if (!digest) { close(fd); qa_range_error("out of memory"); }
  assert_unchanged(&probe);
  bool same = strcmp(digest, mounted->mount.archive_digest) == 0;
  free(digest);
  if (!same) { close(fd); qa_range_error("Download archive changed after mount"); }
  download_file *file = malloc(sizeof *file);
  if (!file) { close(fd); return false; }
  *file = probe;
  out->size = original.st_size; out->read = download_read;
  out->close = download_close; out->ctx = file;
  return true;
}
