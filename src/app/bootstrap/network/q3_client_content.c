/* Port of src/app/bootstrap/network/q3-client-content.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_client_content.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/pak_references.h"
typedef struct pure_system_info {
  bool pure; char *game; int64_t *checksums; size_t checksum_count;
  uint32_t checksum_feed;
} pure_system_info;
struct qa_app_q3_client_content {
  qa_app_q3_content_host host;
  void *loaded; qa_app_q3_packages *packages;
  bool has_policy; qa_pure_mount_policy policy; qa_content_digest *policy_storage;
  pure_system_info settings; bool closed;
};
static void system_info_free(pure_system_info *info) { free(info->game); free(info->checksums); }
static pure_system_info system_info(const char *info, double checksum_feed) {
  if (checksum_feed != trunc(checksum_feed) || checksum_feed < -2147483648.0
      || checksum_feed > 4294967295.0)
    qa_range_error("Q3 checksum feed must be a 32-bit integer");
  qa_q3_server_pak_set *loaded = NULL;
  if (!qa_q3_server_pak_set_init(&loaded)) qa_range_error("out of memory");
  char *paks = qa_q3_info_value(info, "sv_paks");
  qa_q3_server_pak_set_set_checksums(loaded, paks);
  free(paks);
  size_t count = 0;
  const int64_t *sums = qa_q3_server_pak_set_checksums(loaded, &count);
  pure_system_info out = {0};
  out.checksums = malloc((count ? count : 1) * sizeof *out.checksums);
  if (!out.checksums) qa_range_error("out of memory");
  if (count) memcpy(out.checksums, sums, count * sizeof *sums);
  out.checksum_count = count;
  qa_q3_server_pak_set_destroy(loaded);
  char *pure = qa_q3_info_value(info, "sv_pure");
  out.pure = qa_native_atoi(pure) != 0;
  free(pure);
  char *game = qa_q3_info_value(info, "fs_game");
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make("q3-baseq3", game, &selection))
    qa_range_error("out of memory");
  free(game);
  out.game = selection.directory; /* owned; base is a borrowed literal */
  out.checksum_feed = (uint32_t)(int64_t)checksum_feed;
  return out;
}
typedef struct collect_ctx {
  qa_app_q3_packages *packages;
  const qa_resolved_resource_reference **refs; bool *flags;
  size_t count, capacity;
} collect_ctx;
static void collect_visit(void *ctx, const qa_resolved_resource_reference *reference, bool is_q3) {
  collect_ctx *c = ctx;
  if (c->count == c->capacity) {
    c->capacity = c->capacity ? c->capacity * 2 : 16;
    c->refs = realloc(c->refs, c->capacity * sizeof *c->refs);
    c->flags = realloc(c->flags, c->capacity * sizeof *c->flags);
    if (!c->refs || !c->flags) qa_range_error("out of memory");
  }
  c->refs[c->count] = reference; c->flags[c->count] = is_q3; c->count++;
}
static bool collect_is_q3(void *ctx, const qa_resolved_resource_reference *reference) {
  const bool *flag = ctx;
  (void)reference;
  return *flag;
}
qa_app_q3_client_content *qa_app_q3_client_content_open(qa_app_q3_content_host host,
    const char *info, double checksum_feed) {
  pure_system_info settings = system_info(info, checksum_feed);
  if (strcmp(host.product_family, "q3") != 0) {
    system_info_free(&settings);
    qa_range_error("Q3 client content requires a Q3 product");
  }
  const char *slash = strrchr(host.product_content_directory, '/');
  const char *tail = slash ? slash + 1 : host.product_content_directory;
  size_t length = strlen(tail);
  char *game = malloc(length + 1);
  if (!game) { system_info_free(&settings); return NULL; }
  for (size_t i = 0; i < length; i++) game[i] = tolower((unsigned char)tail[i]);
  game[length] = '\0';
  bool matches = strcmp(settings.game, game) == 0;
  free(game);
  if (!matches) {
    system_info_free(&settings);
    qa_range_error("Server game directory differs from the selected Q3 content");
  }
  qa_app_q3_client_content *content = calloc(1, sizeof *content);
  if (!content) { system_info_free(&settings); return NULL; }
  content->host = host; content->settings = settings;
  qa_app_q3_packages *catalog = qa_app_q3_packages_open(
      host.catalog_mounts, host.catalog_mount_count, checksum_feed);
  if (!catalog) { qa_app_q3_client_content_destroy(content); return NULL; }
  qa_pure_mount_policy policy; qa_content_digest *storage = NULL;
  bool has_policy = settings.checksum_count != 0;
  if (has_policy) {
    if (!qa_q3_content_references_pure_mount_policy(
            qa_app_q3_packages_references(catalog), settings.checksums,
            settings.checksum_count, &policy, &storage))
      qa_range_error("out of memory");
  }
  void *loaded = host.load_content(host.ctx, has_policy ? &policy : NULL);
  content->loaded = loaded;
  size_t mount_count = 0;
  const qa_app_q3_package_mount *mounts = host.loaded_mounts(host.ctx, loaded, &mount_count);
  content->packages = qa_app_q3_packages_open(mounts, mount_count, checksum_feed);
  if (!content->packages) {
    host.close_content(host.ctx, loaded);
    content->loaded = NULL;
    qa_app_q3_packages_destroy(catalog);
    qa_app_q3_client_content_destroy(content);
    return NULL;
  }
  content->has_policy = has_policy;
  if (has_policy) { content->policy = policy; content->policy_storage = storage; }
  qa_app_q3_packages_destroy(catalog);
  return content;
}
void qa_app_q3_client_content_destroy(qa_app_q3_client_content *content) {
  if (!content) return;
  qa_app_q3_client_content_close(content);
  qa_app_q3_packages_destroy(content->packages);
  free(content->policy_storage);
  system_info_free(&content->settings);
  free(content);
}
void *qa_app_q3_client_content_loaded(const qa_app_q3_client_content *content) { return content->loaded; }
qa_app_q3_packages *qa_app_q3_client_content_packages(qa_app_q3_client_content *content) {
  return content->packages;
}
bool qa_app_q3_client_content_has_policy(const qa_app_q3_client_content *content) {
  return content->has_policy;
}
bool qa_app_q3_client_content_pure(const qa_app_q3_client_content *content) {
  return content->settings.pure;
}
bool qa_app_q3_client_content_matches(qa_app_q3_client_content *content,
    const char *info, double checksum_feed) {
  if (content->closed) return false;
  pure_system_info next = system_info(info, checksum_feed);
  const pure_system_info *previous = &content->settings;
  bool same = next.pure == previous->pure && strcmp(next.game, previous->game) == 0
      && next.checksum_feed == previous->checksum_feed
      && next.checksum_count == previous->checksum_count;
  if (same)
    for (size_t i = 0; i < next.checksum_count; i++)
      if ((uint32_t)next.checksums[i] != (uint32_t)previous->checksums[i]) same = false;
  system_info_free(&next);
  return same;
}
char *qa_app_q3_client_content_referenced_pure_command(
    qa_app_q3_client_content *content, double server_id) {
  if (content->closed) qa_range_error("Q3 client content is closed");
  collect_ctx collect = {0};
  collect.packages = content->packages;
  content->host.collect_opened(content->host.ctx, content->loaded, collect_visit, &collect);
  for (size_t i = 0; i < collect.count; i++)
    qa_app_q3_packages_collect(content->packages, &collect.refs[i], 1,
        collect_is_q3, &collect.flags[i]);
  free(collect.refs); free(collect.flags);
  if (content->settings.pure) {
    qa_q3_pak_reference_snapshot *snapshot = NULL; size_t count = 0;
    qa_q3_pak_references *references = qa_q3_content_references_get_references(
        qa_app_q3_packages_references(content->packages));
    if (!qa_q3_pak_references_snapshot(references, &snapshot, &count))
      qa_range_error("out of memory");
    static const struct { int64_t flag; const char *name; } required[] = {
      {QA_Q3_PAK_FLAG_CGAME, "cgame"}, {QA_Q3_PAK_FLAG_UI, "UI"}};
    for (size_t r = 0; r < 2; r++) {
      bool found = false;
      for (size_t i = 0; i < count; i++)
        if (snapshot[i].flags & required[r].flag) found = true;
      if (!found) {
        free(snapshot);
        qa_range_error("Pure Q3 admission requires an actually loaded %s module", required[r].name);
      }
    }
    free(snapshot);
  }
  return qa_q3_content_references_referenced_pure_command(
      qa_app_q3_packages_references(content->packages), (int64_t)server_id);
}
void qa_app_q3_client_content_close(qa_app_q3_client_content *content) {
  if (content->closed) return;
  content->closed = true;
  if (content->loaded) {
    content->host.close_content(content->host.ctx, content->loaded);
    content->loaded = NULL;
  }
}
