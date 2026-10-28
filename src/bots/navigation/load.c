/* Port of src/bots/navigation/load.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/load.h"
#include "qa/bots/navigation/graph.h"
#include "qa/core/md4.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void qa_prepared_navigation_destroy(qa_prepared_navigation *prepared) {
  if (!prepared) return;
  if (prepared->aas) { qa_aas_asset_destroy(prepared->aas); free(prepared->aas); }
  if (prepared->kex) { qa_kex_navigation_asset_destroy(prepared->kex); free(prepared->kex); }
  memset(prepared, 0, sizeof *prepared);
}
void qa_loaded_navigation_destroy(qa_loaded_navigation *loaded) {
  if (!loaded) return;
  qa_navigation_runtime_destroy(&loaded->runtime);
  qa_navigation_graph_destroy(&loaded->graph);
  if (loaded->owned_aas) { qa_aas_asset_destroy(loaded->owned_aas); free(loaded->owned_aas); }
  if (loaded->owned_kex) { qa_kex_navigation_asset_destroy(loaded->owned_kex); free(loaded->owned_kex); }
  memset(loaded, 0, sizeof *loaded);
}
static bool ci_starts_with(const char *s, const char *prefix) {
  for (size_t i = 0; prefix[i]; i++)
    if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
  return true;
}
static bool ci_ends_with(const char *s, size_t length, const char *suffix) {
  size_t n = strlen(suffix);
  if (length < n) return false;
  for (size_t i = 0; i < n; i++)
    if (tolower((unsigned char)s[length - n + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
static const char *provenance_content(const qa_resolved_resource_reference *reference) {
  if (reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE && reference->provenance.archive_mount)
    return reference->provenance.archive_mount->identity.content;
  if (reference->provenance.kind == QA_CONTENT_MOUNT_LOOSE && reference->provenance.loose_mount)
    return reference->provenance.loose_mount->identity.content;
  return NULL;
}
/* TS try { parse } catch { continue }: probe the aborting parser in a
 * child; parse again in-process only when the probe succeeded. */
static bool probe_parse(bool is_aas, uint8_t *bytes, size_t length, const char *path, int64_t checksum) {
  pid_t pid = fork();
  if (pid < 0) return true; /* no fork: fall back to direct (aborting) parse */
  if (pid == 0) {
    (void)prctl(PR_SET_DUMPABLE, 0);
    FILE *null_file = fopen("/dev/null", "w");
    if (null_file) { (void)dup2(fileno(null_file), STDERR_FILENO); }
    if (is_aas) {
      qa_aas_asset asset;
      if (!qa_parse_aas(&asset, bytes, length, path, true, checksum)) _exit(2);
    } else {
      qa_kex_navigation_asset asset;
      if (!qa_parse_kex_navigation(&asset, bytes, length, path)) _exit(2);
    }
    _exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return true;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
bool qa_preload_navigation(qa_prepared_navigation *out, qa_navigation_map_identity map,
                           qa_content_mounted *resources, const uint8_t *map_bytes, size_t map_byte_length,
                           bool has_navigation_content, qa_content_id navigation_content) {
  memset(out, 0, sizeof *out);
  out->map = map;
  const char *name = map.name ? map.name : "";
  const char *relative = ci_starts_with(name, "maps/") ? name + 5 : name;
  size_t length = strlen(relative);
  if (ci_ends_with(relative, length, ".bsp")) length -= 4;
  /* validate: non-empty, no empty/dot/dotdot components */
  bool invalid = length == 0;
  char *copy = malloc(length + 1);
  if (!copy) return false;
  memcpy(copy, relative, length); copy[length] = 0;
  if (!invalid) {
    size_t start = 0;
    for (size_t i = 0; i <= length; i++) {
      if (i == length || copy[i] == '/') {
        size_t part = i - start;
        if (part == 0 || (part == 1 && copy[start] == '.') || (part == 2 && copy[start] == '.' && copy[start + 1] == '.')) invalid = true;
        start = i + 1;
      }
    }
  }
  if (invalid) { free(copy); qa_range_error("Invalid navigation map resource path"); }
  char first_path[1024], second_path[1024];
  const char *paths[2];
  if (map.format == QA_DECODED_WORLD_Q3) {
    snprintf(first_path, sizeof first_path, "maps/%s.aas", copy);
    snprintf(second_path, sizeof second_path, "bots/navigation/%s.nav", copy);
  } else {
    snprintf(first_path, sizeof first_path, "bots/navigation/%s.nav", copy);
    snprintf(second_path, sizeof second_path, "maps/%s.aas", copy);
  }
  free(copy);
  paths[0] = first_path; paths[1] = second_path;
  for (int i = 0; i < 2; i++) {
    const char *path = paths[i];
    qa_content_opened_resource opened;
    if (!qa_content_mounted_open(resources, path, NULL, NULL, &opened)) continue;
    bool is_aas = ci_ends_with(path, strlen(path), ".aas");
    bool is_nav = ci_ends_with(path, strlen(path), ".nav");
    if (is_nav && has_navigation_content) {
      const char *content = provenance_content(opened.reference);
      if (!content || !navigation_content || strcmp(content, navigation_content)) { free(opened.bytes); continue; }
    }
    int64_t checksum = (int64_t)(int32_t)qa_block_checksum(map_bytes, map_byte_length);
    if (!probe_parse(is_aas, opened.bytes, opened.byte_length, path, checksum)) { free(opened.bytes); continue; }
    if (is_aas) {
      qa_aas_asset *asset = malloc(sizeof *asset);
      if (!asset) { free(opened.bytes); return false; }
      if (!qa_parse_aas(asset, opened.bytes, opened.byte_length, path, true, checksum)) {
        free(asset); free(opened.bytes); return false;
      }
      out->aas = asset;
    } else {
      qa_kex_navigation_asset *asset = malloc(sizeof *asset);
      if (!asset) { free(opened.bytes); return false; }
      if (!qa_parse_kex_navigation(asset, opened.bytes, opened.byte_length, path)) {
        free(asset); free(opened.bytes); return false;
      }
      out->kex = asset;
    }
    out->resource = opened.reference;
    free(opened.bytes);
    return true;
  }
  return true;
}
bool qa_load_prepared_navigation(qa_loaded_navigation *out, qa_navigation_construction options,
                                 const qa_prepared_navigation *prepared) {
  memset(out, 0, sizeof *out);
  qa_navigation_map_identity map = options.map;
  bool digest_eq = map.digest && prepared->map.digest && !strcmp(map.digest, prepared->map.digest);
  bool name_eq = map.name && prepared->map.name && !strcmp(map.name, prepared->map.name);
  if (map.format != options.geometry.kind || !digest_eq || !name_eq || map.format != prepared->map.format)
    qa_range_error("Navigation map identity and prepared geometry disagree");
  bool ok;
  if (!prepared->aas && !prepared->kex)
    ok = qa_construct_navigation(&out->graph, options);
  else
    ok = qa_navigation_from_asset(&out->graph, map, prepared->aas, prepared->kex, options.profile, options.world);
  if (!ok) return false;
  if (!qa_navigation_runtime_init(&out->runtime, &out->graph, options.world)) {
    qa_navigation_graph_destroy(&out->graph);
    return false;
  }
  out->resource = prepared->resource;
  return true;
}
bool qa_load_navigation(qa_loaded_navigation *out, qa_navigation_load_options options) {
  if (options.construction.map.format != options.construction.geometry.kind)
    qa_range_error("Navigation map identity and decoded geometry disagree");
  qa_prepared_navigation prepared;
  if (!qa_preload_navigation(&prepared, options.construction.map, options.resources,
                             options.map_bytes, options.map_byte_length,
                             options.has_navigation_content, options.navigation_content))
    return false;
  bool ok = qa_load_prepared_navigation(out, options.construction, &prepared);
  if (ok) {
    /* The graph borrows the prepared asset; transfer ownership so it
     * outlives the loaded graph and is freed with it. */
    out->owned_aas = prepared.aas; prepared.aas = NULL;
    out->owned_kex = prepared.kex; prepared.kex = NULL;
  }
  qa_prepared_navigation_destroy(&prepared);
  return ok;
}
