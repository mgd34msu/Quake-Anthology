/* The C-only content, identity and scene accessors of the typed remote
 * presentations (remote_access.h). Each one is the TS expression named on its
 * declaration. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_access.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/content/mounts.h"
#include "qa/content/mounts/paths.h"
#include "qa/formats/q1_map/types.h"
#include "qa/formats/q2_map.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/q3/settings.h"

struct qa_app_remote_q2_mounts {
  qa_app_q2_remote_mounts pub;
  qa_app_remote_content_mounts *owner;
  qa_content_catalog *catalog;
  const qa_catalog_discovered_product *product;
  qa_content_mounted *mounts;
  const char *write_root;
  bool scoped; /* during open_scoped: accept only the game-local roots */
  char *roots[3]; size_t root_count;
};

static void *xcalloc(size_t count, size_t size) {
  void *out = calloc(count ? count : 1, size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static char *xdup(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}

void qa_app_remote_access_init(qa_app_remote_access *access, const qa_identity_owner *identity) {
  memset(access, 0, sizeof *access);
  access->identity = identity;
  access->session = identity->session;
}
static void q2_mounts_free(qa_app_remote_q2_mounts *view) {
  for (size_t i = 0; i < view->root_count; i++) free(view->roots[i]);
  free(view);
}
void qa_app_remote_access_release(qa_app_remote_access *access) {
  free(access->read);
  access->read = NULL;
  for (size_t i = 0; i < access->q2_mount_count; i++) q2_mounts_free(access->q2_mounts[i]);
  free(access->q2_mounts);
  access->q2_mounts = NULL;
  access->q2_mount_count = 0;
}

/* ---- RemoteWorldContent ------------------------------------------------- */
qa_decoded_world qa_app_remote_decoded_world(const qa_app_loaded_content *content) {
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_decoded_world out;
  memset(&out, 0, sizeof out);
  if (strcmp(world.kind, "q1-bsp") == 0) {
    out.kind = QA_DECODED_WORLD_Q1;
    out.as.q1 = &((const qa_q1_map *)world.value)->world;
  } else if (strcmp(world.kind, "q2-bsp") == 0) {
    out.kind = QA_DECODED_WORLD_Q2;
    out.as.q2 = &((const qa_q2_decoded_map *)world.value)->world;
  } else {
    out.kind = QA_DECODED_WORLD_Q3;
    out.as.q3 = world.value;
  }
  return out;
}
static void *world_create_queries(void *ctx, void *loaded) {
  (void)ctx; /* the world host is stateless: TS createSceneQueries(content.world) */
  qa_shared_scene_queries *queries = qa_create_scene_queries(qa_app_remote_decoded_world(loaded));
  if (queries == NULL) qa_range_error("Out of memory");
  return queries;
}
static void world_bind_settings(void *ctx, void *queries, void *settings) {
  (void)ctx;
  qa_shared_bind_collision_settings(queries, settings);
}
static void world_register_map(void *ctx, void *settings) {
  (void)ctx;
  qa_collision_map_settings_register_map(settings);
}
static void world_destroy_queries(void *ctx, void *queries) {
  (void)ctx;
  qa_shared_scene_queries_destroy(queries);
}
qa_app_remote_world_host qa_app_remote_access_world_host(void) {
  qa_app_remote_world_host host;
  memset(&host, 0, sizeof host);
  host.create_scene_queries = world_create_queries;
  host.bind_collision_settings = world_bind_settings;
  host.register_map = world_register_map;
  host.destroy_queries = world_destroy_queries;
  return host;
}

/* ---- identity ------------------------------------------------------------ */
double qa_app_remote_access_next_generation(void *ctx, double slot) {
  qa_app_remote_access *access = ctx;
  if (!(slot >= 0 && slot <= 9007199254740991.0 && slot == (double)(int64_t)slot))
    qa_range_error("Actor slot must be a nonnegative safe integer");
  return (double)qa_next_actor_generation(access->session, (int64_t)slot, 0);
}
qa_actor_id qa_app_remote_access_identity_actor(void *ctx, double slot, double generation) {
  qa_app_remote_access *access = ctx;
  return qa_identity_owner_actor(access->identity, (int64_t)slot, (int64_t)generation);
}
qa_client_id qa_app_remote_access_identity_client(void *ctx, double slot, double generation) {
  qa_app_remote_access *access = ctx;
  return qa_identity_owner_client(access->identity, (int64_t)slot, (int64_t)generation);
}
qa_owned_actor qa_app_remote_access_owned_actor(void *ctx, qa_actor_id actor, const void *provider) {
  qa_app_remote_access *access = ctx;
  const qa_provider_reference *reference = provider;
  return qa_identity_owner_owned_actor(access->identity, actor, reference->provider);
}

/* ---- content -------------------------------------------------------------- */
const qa_executable_recipe *qa_app_remote_access_content_recipe(void *ctx, void *loaded) {
  (void)ctx;
  return qa_app_loaded_content_recipe(loaded);
}
const qa_scene_snapshot_world *qa_app_remote_access_content_world(void *ctx, void *loaded) {
  qa_app_remote_access *access = ctx;
  memset(&access->world, 0, sizeof access->world);
  access->world.resource = qa_app_loaded_content_recipe(loaded)->map.geometry;
  access->world.geometry = qa_app_remote_decoded_world(loaded);
  return &access->world;
}
const uint8_t *qa_app_remote_access_content_read(void *ctx, void *loaded,
    const qa_resolved_resource_reference *reference, size_t *out_length) {
  qa_app_remote_access *access = ctx;
  qa_content_mounted *mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(loaded);
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, reference, &length);
  free(access->read);
  access->read = bytes;
  access->read_length = length;
  *out_length = length;
  return bytes;
}

/* ---- scene -------------------------------------------------------------- */
void qa_app_remote_access_scene_link(void *ctx, void *scene, qa_linked_body body, qa_actor_collision collision) {
  (void)ctx;
  qa_shared_link(scene, body, collision);
}
void qa_app_remote_access_scene_unlink(void *ctx, void *scene, qa_actor_id actor) {
  (void)ctx;
  qa_shared_unlink(scene, actor);
}
qa_bounds qa_app_remote_access_scene_model_bounds(void *ctx, void *scene, double model) {
  (void)ctx;
  return qa_shared_model_bounds(scene, model);
}
bool qa_app_remote_access_scene_actor_is_model(void *ctx, void *scene, qa_actor_id actor) {
  (void)ctx;
  qa_spatial_actor linked;
  if (!qa_shared_linked_actor(scene, actor, &linked)) return false;
  return linked.collision.shape_kind != NULL && strcmp(linked.collision.shape_kind, "model") == 0;
}
qa_app_qw_camera_trace qa_app_remote_access_qw_camera_trace(void *ctx, void *scene, qa_vec3 start, qa_vec3 end,
    bool has_pass_actor, qa_actor_id pass_actor) {
  qa_app_remote_access *access = ctx;
  if (access->loaded == NULL) qa_raise("Error", 0, 0, "Remote server has not supplied a world");
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape.kind = "box";
  query.shape.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = "no-monsters";
  query.policy.has_hull = true;
  query.policy.hull = 1;
  query.numeric = qa_app_sim_movement_profile(*qa_app_loaded_content_recipe(access->loaded)).numeric;
  query.pass_actor = has_pass_actor ? &pass_actor : NULL;
  qa_trace_result trace = qa_shared_trace(scene, query);
  if (trace.kind == NULL || strcmp(trace.kind, "q1") != 0) qa_raise("Error", 0, 0, "QW camera requires Q1 collision");
  qa_app_qw_camera_trace out;
  out.fraction = trace.start_solid ? 0 : trace.fraction;
  out.end = trace.end;
  out.in_water = trace.in_water;
  return out;
}
bool qa_app_remote_access_resolve_sound(void *ctx, void *loaded, const char *path) {
  (void)ctx;
  const qa_resolved_resource_reference *resolved = NULL;
  return qa_content_mounted_resolve((qa_content_mounted *)qa_app_loaded_content_mounts(loaded), path, &resolved);
}
double qa_app_remote_access_now_milliseconds(void *ctx) {
  (void)ctx;
  return qa_app_performance_now();
}

/* ---- unified -------------------------------------------------------------- */
const qa_prepared_mod *qa_app_remote_access_prepared_mods(void *ctx, void *loaded, size_t *out_count) {
  (void)ctx;
  size_t count = 0;
  const void *const *mods = qa_app_loaded_content_prepared_mods(loaded, &count);
  *out_count = count;
  /* the content's rows are one array; its pointer list addresses it in order */
  return count == 0 ? NULL : (const qa_prepared_mod *)mods[0];
}
bool qa_app_remote_access_mod_has_presentation(void *ctx, void *loaded) {
  size_t count = 0;
  const qa_prepared_mod *mods = qa_app_remote_access_prepared_mods(ctx, loaded, &count);
  for (size_t i = 0; i < count; i++)
    if (mods[i].has_presentation) return true;
  return false;
}
const void *qa_app_remote_access_map_entities_provider(void *ctx, void *loaded) {
  (void)ctx;
  return &qa_app_loaded_content_recipe(loaded)->map.entities;
}
static qa_content_id mount_content(const qa_resolved_resource_reference *reference) {
  return reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE ? reference->provenance.archive_mount->identity.content
                                                                 : reference->provenance.loose_mount->identity.content;
}
qa_resolved_resource_reference qa_app_remote_access_resolve_unified_resource(void *ctx, void *loaded,
    const qa_app_unified_resource_key *key) {
  (void)ctx;
  char *normal = qa_normalize_resource_path(key->path);
  if (normal == NULL) qa_range_error("Out of memory");
  bool same = strcmp(normal, key->path) == 0;
  free(normal);
  if (!same) qa_raise("Error", 0, 0, "Unified paths must use forward slashes");
  qa_content_mounted *mounted = (qa_content_mounted *)qa_app_loaded_content_for_content(loaded, key->content);
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  bool found = qa_content_mounted_open(mounted, key->path, NULL, NULL, &opened);
  free(opened.bytes);
  if (!found || strcmp(mount_content(opened.reference), key->content) != 0
      || strcmp(opened.reference->digest, key->digest) != 0 || (double)opened.reference->byte_length != key->byte_length)
    qa_raise("Error", 0, 0, "Unified resource is absent or differs: %s/%s", key->content, key->path);
  return *opened.reference;
}
qa_resolved_resource_reference qa_app_remote_access_world_resource(void *ctx, void *loaded) {
  (void)ctx;
  return qa_app_loaded_content_recipe(loaded)->map.geometry;
}

/* ---- Q2 RemoteContentMounts view ------------------------------------------ */
static char *normalize_absolute(const char *joined) {
  size_t length = strlen(joined);
  char *copy = xdup(joined);
  char **parts = xcalloc(length / 2 + 2, sizeof *parts);
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    size_t part_length = (size_t)(cursor - start);
    if (*cursor == '/') *cursor++ = '\0';
    if (part_length == 1 && start[0] == '.') continue;
    if (part_length == 2 && start[0] == '.' && start[1] == '.') {
      if (count > 0) count--;
      continue;
    }
    parts[count++] = start;
  }
  char *out = xcalloc(length + 2, 1);
  size_t at = 0;
  if (count == 0) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    out[at++] = '/';
    size_t part_length = strlen(parts[i]);
    memcpy(out + at, parts[i], part_length);
    at += part_length;
  }
  out[at] = '\0';
  free(parts);
  free(copy);
  return out;
}
/* node:path resolve(first, second?) (lexical). */
static char *path_resolve(const char *first, const char *second) {
  char cwd[PATH_MAX];
  const char *base = first;
  if (first[0] != '/' && (second == NULL || second[0] != '/')) {
    if (getcwd(cwd, sizeof cwd) == NULL) qa_raise("Error", 0, 0, "Working directory is unavailable");
  } else cwd[0] = '\0';
  size_t size = strlen(cwd) + strlen(first) + (second ? strlen(second) : 0) + 4;
  char *joined = xcalloc(size, 1);
  if (second != NULL && second[0] == '/') snprintf(joined, size, "%s", second);
  else if (base[0] == '/') snprintf(joined, size, second ? "%s/%s" : "%s", base, second ? second : "");
  else snprintf(joined, size, second ? "%s/%s/%s" : "%s/%s", cwd, base, second ? second : "");
  char *out = normalize_absolute(joined);
  free(joined);
  return out;
}
static char *path_dirname(const char *path) {
  char *copy = xdup(path);
  size_t length = strlen(copy);
  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  char *slash = strrchr(copy, '/');
  if (slash == NULL) { free(copy); return xdup("."); }
  if (slash == copy) { copy[1] = '\0'; return copy; }
  *slash = '\0';
  return copy;
}
static bool q2_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length,
    const qa_resolved_resource_reference **out_reference) {
  qa_app_remote_q2_mounts *view = ctx;
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(view->mounts, path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes;
  *out_length = opened.byte_length;
  if (out_reference != NULL) *out_reference = opened.reference;
  return true;
}
static bool q2_exists(void *ctx, const char *path) {
  qa_app_remote_q2_mounts *view = ctx;
  return qa_content_mounted_exists(view->mounts, path);
}
static void q2_invalidate(void *ctx) {
  qa_app_remote_q2_mounts *view = ctx;
  qa_content_mounted_invalidate_listings(view->mounts);
}
/* TS mount => roots.has(resolve(mount.kind === "archive" ? dirname(mount.archivePath) : mount.rootPath)). */
static bool q2_accept_root(void *ctx, const qa_content_mount *mount) {
  qa_app_remote_q2_mounts *view = ctx;
  char *directory = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? path_dirname(mount->as.archive.archive_path)
                                                            : xdup(mount->as.loose.root_path);
  char *resolved = path_resolve(directory, NULL);
  free(directory);
  bool found = false;
  for (size_t i = 0; i < view->root_count && !found; i++) found = strcmp(view->roots[i], resolved) == 0;
  free(resolved);
  return found;
}
static bool q2_open_scoped(void *ctx, const char *path, bool game_local) {
  qa_app_remote_q2_mounts *view = ctx;
  for (size_t i = 0; i < view->root_count; i++) free(view->roots[i]);
  view->root_count = 0;
  view->roots[view->root_count++] = path_resolve(view->catalog->corpus_root, view->product->expectation.content_directory);
  char *write = path_resolve(view->write_root, NULL);
  bool duplicate = strcmp(write, view->roots[0]) == 0;
  if (duplicate) free(write);
  else view->roots[view->root_count++] = write;
  if (view->product->loose_root != NULL) {
    char *loose = path_resolve(view->product->loose_root, NULL);
    bool seen = false;
    for (size_t i = 0; i < view->root_count; i++) seen = seen || strcmp(view->roots[i], loose) == 0;
    if (seen) free(loose);
    else view->roots[view->root_count++] = loose;
  }
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  bool found = qa_content_mounted_open(view->mounts, path, game_local ? q2_accept_root : NULL, view, &opened);
  free(opened.bytes);
  return found;
}
static qa_app_remote_q2_mounts *q2_view(qa_app_remote_access *access, qa_app_remote_content_mounts *owner,
    qa_content_catalog *catalog, const qa_catalog_discovered_product *product, qa_content_mounted *mounts) {
  qa_app_remote_q2_mounts *view = xcalloc(1, sizeof *view);
  view->owner = owner;
  view->catalog = catalog;
  view->product = product;
  view->mounts = mounts;
  view->write_root = owner->write_root;
  view->pub.mounts.ctx = view;
  view->pub.mounts.open = q2_open;
  view->pub.mounts.exists = q2_exists;
  view->pub.mounts.invalidate_listings = q2_invalidate;
  view->pub.selection_base = owner->selection.base;
  view->pub.selection_directory = owner->selection.directory;
  view->pub.write_root = owner->write_root;
  view->pub.base_write_root = owner->base_write_root;
  view->pub.corpus_root = catalog->corpus_root;
  view->pub.product_content_directory = product->expectation.content_directory;
  view->pub.product_loose_root = product->loose_root;
  view->pub.open_scoped = q2_open_scoped;
  qa_app_remote_q2_mounts **grown = realloc(access->q2_mounts, (access->q2_mount_count + 1) * sizeof *grown);
  if (grown == NULL) { free(view); qa_range_error("Out of memory"); }
  access->q2_mounts = grown;
  access->q2_mounts[access->q2_mount_count++] = view;
  return view;
}
const qa_app_q2_remote_mounts *qa_app_remote_access_q2_mounts(qa_app_remote_access *access,
    qa_app_remote_content_mounts *owner) {
  return &q2_view(access, owner, owner->catalog, owner->product, owner->mounts)->pub;
}
/* TS { ...owner, catalog: content.catalog, product: content.catalog.product(owner.product.id), mounts: content.mounts }. */
const qa_app_q2_remote_mounts *qa_app_remote_access_rebind_q2_mounts(void *ctx, const qa_app_q2_remote_mounts *owner,
    void *loaded) {
  qa_app_remote_access *access = ctx;
  qa_app_remote_q2_mounts *previous = NULL;
  for (size_t i = 0; i < access->q2_mount_count && previous == NULL; i++)
    if (&access->q2_mounts[i]->pub == owner) previous = access->q2_mounts[i];
  if (previous == NULL) qa_raise("Error", 0, 0, "Q2 download owner belongs to another remote");
  qa_content_catalog *catalog = (qa_content_catalog *)qa_app_loaded_content_catalog(loaded);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, previous->product->id);
  return &q2_view(access, previous->owner, catalog, product,
      (qa_content_mounted *)qa_app_loaded_content_mounts(loaded))->pub;
}

/* ---- Q3ApplicationPackages plan mounts ----------------------------------- */
qa_app_q3_package_mount *qa_app_q3_package_mounts_of(const qa_content_catalog *catalog,
    const qa_content_mounted *mounts, size_t *out_count) {
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(mounts);
  qa_app_q3_package_mount *rows = xcalloc(plan->default_order_count, sizeof *rows);
  size_t count = 0;
  for (size_t i = 0; i < plan->default_order_count; i++) {
    const qa_content_mount *mount = NULL;
    for (size_t j = 0; j < plan->mount_count && mount == NULL; j++) {
      const qa_mount_identity *identity = plan->mounts[j].kind == QA_CONTENT_MOUNT_ARCHIVE
          ? &plan->mounts[j].as.archive.identity : &plan->mounts[j].as.loose.identity;
      if (strcmp(identity->id, plan->default_order[i]) == 0) mount = &plan->mounts[j];
    }
    /* TS: mount?.kind !== "archive" || mount.format !== "pk3" -> continue; product family !== "q3" -> continue */
    if (mount == NULL || mount->kind != QA_CONTENT_MOUNT_ARCHIVE || mount->as.archive.format != QA_ARCHIVE_FORMAT_PK3)
      continue;
    const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, mount->as.archive.identity.content);
    qa_app_q3_package_mount *row = &rows[count++];
    row->is_archive = true;
    row->mount = mount->as.archive;
    row->product_family = qa_game_family_name(product->expectation.family);
    row->content_directory = product->expectation.content_directory;
  }
  *out_count = count;
  return rows;
}
