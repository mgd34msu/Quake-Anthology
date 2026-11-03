/* The unified family's content steps of remote-application.ts:
 *  - loadUnifiedContent({ ...options, mode: offer.mode }, offer.composition, mountedContent.catalog)
 *    (unified-content.ts) through network/unified_content.c with the UnifiedContentHost below over the
 *    installed catalog: catalog.mountsFor as ContentMount trees, openMountPlan over the rebound plan,
 *    applicationOptionsForRecipe + loadApplicationContent(configured, recipe, undefined, catalog,
 *    { kind: "unified", family }) for the local recipe, and the loaded recipe / map sidecars
 *    buildUnifiedComposition reads back;
 *  - adoptUnifiedProfile's `{ catalog, mounts, q3Product, close, selection }` over a loaded content
 *    whose close releases the retained main mounts once.
 * GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "remote_application_internal.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static qa_save_value *checked(qa_save_value *value) {
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static void put(qa_save_value *object, const char *key, qa_save_value *value) {
  if (!qa_save_value_set(object, key, checked(value))) { qa_save_value_free(value); qa_range_error("Out of memory"); }
}
static const qa_save_value *get(const qa_save_value *object, const char *key) {
  return object != NULL ? qa_save_value_get(object, key) : NULL;
}
static const char *text(const qa_save_value *object, const char *key) {
  const qa_save_value *value = get(object, key);
  return value != NULL && value->kind == QA_SAVE_STRING ? value->text : NULL;
}

/* ---- the UnifiedContentHost ------------------------------------------------------------- */
typedef struct unified_host_state {
  qa_remote_application *app;
  qa_content_catalog *catalog;      /* mountedContent.catalog */
  qa_app_options options;           /* { ...this.options, mode: offer.mode } (shallow; borrows launch_options) */
  /* catalog.mountsFor results, kept until the load ends (the resolver borrows the trees). */
  struct { qa_catalog_mount_list list; qa_save_value **trees; } *mounts_for; size_t mounts_for_count;
  /* openMountPlan */
  qa_resolved_mount_plan plan; qa_content_mount *plan_mounts; const char **plan_order;
  qa_prefix_mount_order *plan_prefixes; const char ***plan_prefix_mounts;
  qa_content_mounted *opened;
  char *resolved_id;
  /* loadApplicationContent results */
  qa_app_loaded_content *loaded;
  qa_save_value *loaded_recipe;
  qa_app_unified_map_sidecar *sidecars; size_t sidecar_count;
} unified_host_state;

/* catalog.mountsFor's ContentMount records ({ kind, identity, rootPath } | { kind, identity, archivePath,
 * archiveDigest, format }). */
static qa_save_value *mount_identity_tree(qa_mount_identity identity) {
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "id", qa_save_value_string(identity.id)); put(o, "content", qa_save_value_string(identity.content));
  put(o, "generation", qa_save_value_number((double)identity.generation));
  return o;
}
static qa_save_value *mount_tree(const qa_content_mount *mount) {
  static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
  qa_save_value *o = checked(qa_save_value_object());
  if (mount->kind == QA_CONTENT_MOUNT_LOOSE) {
    put(o, "kind", qa_save_value_string("loose")); put(o, "identity", mount_identity_tree(mount->as.loose.identity));
    put(o, "rootPath", qa_save_value_string(mount->as.loose.root_path));
    return o;
  }
  put(o, "kind", qa_save_value_string("archive")); put(o, "identity", mount_identity_tree(mount->as.archive.identity));
  put(o, "archivePath", qa_save_value_string(mount->as.archive.archive_path));
  put(o, "archiveDigest", qa_save_value_string(mount->as.archive.archive_digest));
  put(o, "format", qa_save_value_string(formats[mount->as.archive.format]));
  return o;
}
static const qa_save_value *const *host_catalog_mounts(void *ctx, qa_content_id content, size_t *out_count) {
  unified_host_state *h = ctx;
  void *grown = realloc(h->mounts_for, (h->mounts_for_count + 1) * sizeof *h->mounts_for);
  if (grown == NULL) qa_range_error("Out of memory");
  h->mounts_for = grown;
  memset(&h->mounts_for[h->mounts_for_count], 0, sizeof h->mounts_for[h->mounts_for_count]);
  if (!qa_content_catalog_mounts_for(h->catalog, content, &h->mounts_for[h->mounts_for_count].list))
    qa_range_error("Out of memory");
  size_t at = h->mounts_for_count++;
  qa_catalog_mount_list *list = &h->mounts_for[at].list;
  h->mounts_for[at].trees = xcalloc(list->count, sizeof *h->mounts_for[at].trees);
  for (size_t i = 0; i < list->count; i++) h->mounts_for[at].trees[i] = mount_tree(&list->mounts[i]);
  *out_count = list->count;
  return (const qa_save_value *const *)h->mounts_for[at].trees;
}

/* openMountPlan(plan): the ResolvedMountPlan of the rebound plan tree (strings borrowed from the tree, which
 * lives until close_plan). */
static qa_archive_format archive_format(const char *name) {
  static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
  for (int i = 0; i < 4; i++) if (name != NULL && strcmp(name, formats[i]) == 0) return (qa_archive_format)i;
  qa_raise("Error", 0, 0, "Unknown unified mount archive format %s", name != NULL ? name : "(null)");
}
static void host_close_plan(void *ctx) {
  unified_host_state *h = ctx;
  if (h->opened != NULL) {
    qa_content_mounted *opened = h->opened;
    h->opened = NULL;
    qa_content_mounted_close(opened);
    qa_content_mounted_destroy(opened);
  }
  for (size_t i = 0; i < h->plan.prefix_order_count; i++) free(h->plan_prefix_mounts[i]);
  free(h->plan_prefix_mounts); free(h->plan_prefixes); free(h->plan_order); free(h->plan_mounts);
  h->plan_prefix_mounts = NULL; h->plan_prefixes = NULL; h->plan_order = NULL; h->plan_mounts = NULL;
  memset(&h->plan, 0, sizeof h->plan);
  free(h->resolved_id); h->resolved_id = NULL;
}
static const char **id_list(const qa_save_value *list, size_t *out_count) {
  size_t count = list != NULL && list->kind == QA_SAVE_ARRAY ? list->item_count : 0;
  const char **ids = xcalloc(count, sizeof *ids);
  for (size_t i = 0; i < count; i++) ids[i] = list->items[i]->kind == QA_SAVE_STRING ? list->items[i]->text : "";
  *out_count = count;
  return ids;
}
static void host_open_plan(void *ctx, const qa_save_value *plan) {
  unified_host_state *h = ctx;
  host_close_plan(h);
  const qa_save_value *mounts = get(plan, "mounts");
  size_t mount_count = mounts != NULL && mounts->kind == QA_SAVE_ARRAY ? mounts->item_count : 0;
  h->plan_mounts = xcalloc(mount_count, sizeof *h->plan_mounts);
  for (size_t i = 0; i < mount_count; i++) {
    const qa_save_value *m = mounts->items[i], *identity = get(m, "identity");
    const qa_save_value *generation = get(identity, "generation");
    qa_mount_identity id = {text(identity, "id"), text(identity, "content"),
        generation != NULL && generation->kind == QA_SAVE_NUMBER ? (int64_t)generation->number : 0};
    qa_content_mount *mount = &h->plan_mounts[i];
    if (text(m, "kind") != NULL && strcmp(text(m, "kind"), "archive") == 0) {
      mount->kind = QA_CONTENT_MOUNT_ARCHIVE;
      mount->as.archive.identity = id;
      mount->as.archive.format = archive_format(text(m, "format"));
      mount->as.archive.archive_path = text(m, "archivePath");
      mount->as.archive.archive_digest = text(m, "archiveDigest");
    } else {
      mount->kind = QA_CONTENT_MOUNT_LOOSE;
      mount->as.loose.identity = id;
      mount->as.loose.root_path = text(m, "rootPath");
    }
  }
  size_t order_count = 0;
  h->plan_order = id_list(get(plan, "defaultOrder"), &order_count);
  const qa_save_value *prefixes = get(plan, "prefixOrders");
  size_t prefix_count = prefixes != NULL && prefixes->kind == QA_SAVE_ARRAY ? prefixes->item_count : 0;
  h->plan_prefixes = xcalloc(prefix_count, sizeof *h->plan_prefixes);
  h->plan_prefix_mounts = xcalloc(prefix_count, sizeof *h->plan_prefix_mounts);
  h->plan.prefix_order_count = prefix_count;
  for (size_t i = 0; i < prefix_count; i++) {
    h->plan_prefixes[i].prefix = text(prefixes->items[i], "prefix");
    h->plan_prefix_mounts[i] = id_list(get(prefixes->items[i], "mounts"), &h->plan_prefixes[i].mount_count);
    h->plan_prefixes[i].mounts = h->plan_prefix_mounts[i];
  }
  h->plan.id = text(plan, "id");
  h->plan.mounts = h->plan_mounts; h->plan.mount_count = mount_count;
  h->plan.default_order = h->plan_order; h->plan.default_order_count = order_count;
  h->plan.prefix_orders = h->plan_prefixes;
  h->opened = qa_open_mount_plan(&h->plan, NULL);
  if (h->opened == NULL) qa_range_error("Out of memory");
}
static void release_reference(void *reference) { qa_resolved_resource_release(reference); }
static void host_plan_read(void *ctx, const qa_save_value *resource) {
  unified_host_state *h = ctx;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, resource, "unified.resource")) qa_range_error("Out of memory");
  qa_resolved_resource_reference reference = qa_read_resource(&reader);
  qa_save_reader_destroy(&reader);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_reference, &reference);
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(h->opened, &reference, &length);
  qa_cleanup_pop(&guard);
  qa_resolved_resource_release(&reference);
  free(bytes);
}
static const char *host_plan_resolve_id(void *ctx, const char *requested_path) {
  unified_host_state *h = ctx;
  const qa_resolved_resource_reference *resolved = NULL;
  free(h->resolved_id); h->resolved_id = NULL;
  if (!qa_content_mounted_resolve(h->opened, requested_path, &resolved) || resolved == NULL) return NULL;
  size_t length = strlen(resolved->id) + 1;
  h->resolved_id = malloc(length);
  if (h->resolved_id == NULL) qa_range_error("Out of memory");
  memcpy(h->resolved_id, resolved->id, length);
  return h->resolved_id;
}

/* applicationOptionsForRecipe(options, { catalog, recipe }) + loadApplicationContent(configured, recipe,
 * undefined, catalog, { kind: "unified", family }). The recipe borrows its tree; both live as long as the
 * content. */
typedef struct kept_recipe { qa_save_value *tree; qa_executable_recipe recipe; bool has_recipe; } kept_recipe;
static void kept_recipe_release(void *record) {
  kept_recipe *kept = record;
  if (kept->has_recipe) qa_executable_recipe_free(&kept->recipe);
  qa_save_value_free(kept->tree);
  free(kept);
}
static qa_app_demo_family demo_family(const char *family) {
  if (strcmp(family, "qw") == 0) return QA_APP_DEMO_QW;
  if (strcmp(family, "q2") == 0) return QA_APP_DEMO_Q2;
  if (strcmp(family, "q3") == 0) return QA_APP_DEMO_Q3;
  return QA_APP_DEMO_Q1;
}
static qa_save_value *clone_tree(const qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  if (bytes == NULL) qa_range_error("Out of memory");
  qa_save_value *out = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  return checked(out);
}
typedef struct load_scope { unified_host_state *h; kept_recipe *kept; qa_app_options configured; bool has_configured; } load_scope;
static void load_scope_free(void *arg) {
  load_scope *scope = arg;
  if (scope->kept != NULL) kept_recipe_release(scope->kept);
  if (scope->has_configured) qa_app_options_for_recipe_free(&scope->configured);
}
static void *host_load_content(void *ctx, const qa_save_value *recipe_tree, const char *family) {
  unified_host_state *h = ctx;
  load_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h;
  qa_cleanup guard;
  qa_cleanup_push(&guard, load_scope_free, &scope);
  scope.kept = xcalloc(1, sizeof *scope.kept);
  scope.kept->tree = clone_tree(recipe_tree);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, scope.kept->tree, "unified.recipe")) qa_range_error("Out of memory");
  scope.kept->recipe = qa_read_recipe(&reader);
  scope.kept->has_recipe = true;
  qa_save_reader_destroy(&reader);
  scope.configured = qa_app_application_options_for_recipe(&h->options, h->catalog, &scope.kept->recipe);
  scope.has_configured = true;
  qa_app_options owned;
  qa_app_startup_options_copy(&owned, &scope.configured);
  qa_app_content_source source = {"unified", demo_family(family)};
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, owned,
      &scope.kept->recipe, true, NULL, h->catalog, &source);
  if (content == NULL) qa_range_error("Out of memory");
  qa_app_loaded_content_keep(content, scope.kept, kept_recipe_release);
  scope.kept = NULL;
  qa_cleanup_pop(&guard);
  load_scope_free(&scope);
  h->loaded = content;
  return content;
}
static void host_close_content(void *ctx, void *loaded) {
  unified_host_state *h = ctx;
  if (h->loaded == loaded) h->loaded = NULL;
  qa_app_loaded_content_close(loaded);
  qa_app_loaded_content_destroy(loaded);
}
static const qa_save_value *host_loaded_recipe(void *ctx, void *loaded) {
  unified_host_state *h = ctx;
  qa_save_value_free(h->loaded_recipe);
  h->loaded_recipe = checked(qa_save_recipe_value(qa_app_loaded_content_recipe(loaded)));
  return h->loaded_recipe;
}
static const qa_app_unified_map_sidecar *host_loaded_sidecars(void *ctx, void *loaded, size_t *out_count) {
  unified_host_state *h = ctx;
  size_t count = 0;
  const qa_app_application_map_sidecar *sidecars = qa_app_loaded_content_map_sidecars(loaded, &count);
  free(h->sidecars);
  h->sidecars = xcalloc(count, sizeof *h->sidecars);
  for (size_t i = 0; i < count; i++) {
    h->sidecars[i].content = sidecars[i].content; h->sidecars[i].path = sidecars[i].path;
    h->sidecars[i].has_resource = sidecars[i].resource != NULL;
    if (h->sidecars[i].has_resource) h->sidecars[i].resource = qa_app_unified_resource_key_from_reference(sidecars[i].resource);
  }
  h->sidecar_count = count;
  *out_count = count;
  return h->sidecars;
}
static const qa_catalog_product_expectation *product_of(unified_host_state *h, qa_content_id content) {
  return &qa_content_catalog_product(h->catalog, content)->expectation;
}
static const char *host_loaded_family(void *ctx, void *loaded) {
  unified_host_state *h = ctx;
  const qa_catalog_product_expectation *product = product_of(h, qa_app_loaded_content_recipe(loaded)->map.entities.content);
  if (product->family == QA_GAME_FAMILY_Q1 && product->edition != NULL && strcmp(product->edition, "quakeworld") == 0)
    return "qw";
  return qa_game_family_name(product->family);
}
static const char *host_map_entities_content(void *ctx, const qa_save_value *recipe) {
  (void)ctx;
  return text(get(get(recipe, "map"), "entities"), "content");
}
static const char *host_product_family(void *ctx, qa_content_id content) {
  return qa_game_family_name(product_of(ctx, content)->family);
}
static const char *host_product_edition(void *ctx, qa_content_id content) {
  const char *edition = product_of(ctx, content)->edition;
  return edition != NULL ? edition : "";
}
/* resolveUnifiedResource's content.forContent(key.content).open(key.path), over the content this host loaded. */
static bool host_open_resource(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference *out) {
  unified_host_state *h = ctx;
  if (h->loaded == NULL) qa_raise("Error", 0, 0, "Unified resource has no loaded content");
  qa_content_mounted *mounted = (qa_content_mounted *)qa_app_loaded_content_for_content(h->loaded, content);
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  bool found = qa_content_mounted_open(mounted, path, NULL, NULL, &opened);
  free(opened.bytes);
  if (found) *out = *opened.reference;
  return found;
}

static void host_state_free(void *arg) {
  unified_host_state *h = arg;
  host_close_plan(h);
  for (size_t i = 0; i < h->mounts_for_count; i++) {
    for (size_t j = 0; j < h->mounts_for[i].list.count; j++) qa_save_value_free(h->mounts_for[i].trees[j]);
    free(h->mounts_for[i].trees);
    qa_content_catalog_mount_list_free(&h->mounts_for[i].list);
  }
  free(h->mounts_for); h->mounts_for = NULL; h->mounts_for_count = 0;
  qa_save_value_free(h->loaded_recipe); h->loaded_recipe = NULL;
  free(h->sidecars); h->sidecars = NULL; h->sidecar_count = 0;
}
typedef struct load_call { unified_host_state *h; qa_app_unified_content_host host; qa_save_value *identity; qa_app_loaded_content *result; } load_call;
static void load_call_free(void *arg) {
  load_call *call = arg;
  qa_save_value_free(call->identity);
  call->identity = NULL;
  host_state_free(call->h);
}
qa_app_loaded_content *qa_remote_load_unified_content(qa_remote_application *app, const qa_app_unified_control *offer) {
  if (offer->composition == NULL) qa_raise("Error", 0, 0, "Unified offer has no composition");
  unified_host_state h;
  memset(&h, 0, sizeof h);
  h.app = app;
  h.catalog = (qa_content_catalog *)qa_app_configuration_content_catalog(app->mounted_content);
  if (h.catalog == NULL) qa_raise("Error", 0, 0, "Unified content requires the installed catalog");
  h.options = app->launch_options;
  h.options.mode = offer->mode;
  load_call call;
  memset(&call, 0, sizeof call);
  call.h = &h;
  qa_app_unified_content_host *host = &call.host;
  host->ctx = &h;
  host->catalog_mounts = host_catalog_mounts;
  host->open_plan = host_open_plan;
  host->close_plan = host_close_plan;
  host->plan_read = host_plan_read;
  host->plan_resolve_id = host_plan_resolve_id;
  host->load_content = host_load_content;
  host->close_content = host_close_content;
  host->loaded_recipe = host_loaded_recipe;
  host->loaded_sidecars = host_loaded_sidecars;
  host->loaded_family = host_loaded_family;
  host->map_entities_content = host_map_entities_content;
  host->product_family = host_product_family;
  host->product_edition = host_product_edition;
  host->open_resource = host_open_resource;
  qa_cleanup guard;
  qa_cleanup_push(&guard, load_call_free, &call);
  call.identity = checked(qa_app_unified_composition_value(offer->composition));
  qa_app_loaded_content *content = qa_app_unified_load_content(call.identity, host);
  qa_cleanup_pop(&guard);
  load_call_free(&call);
  return content;
}

/* ---- adoptUnifiedProfile's mounted content ------------------------------------------------ */
/* TS `close = async () => { if (!released) { released = true; release(); } }`. */
typedef struct mounted_record { qa_app_main_mount_lease lease; bool released; qa_app_configuration_content *content; } mounted_record;
static void mounted_close(void *ctx) {
  mounted_record *record = ctx;
  if (record->released) return;
  record->released = true;
  if (record->lease.release != NULL) record->lease.release(record->lease.ctx);
}
/* Application teardown (TS GC): the configuration that took the content is dropped with the application, after
 * the loaded content it shares is gone, so dropping it releases no lease. */
static void mounted_drop(void *object) {
  mounted_record *record = object;
  record->released = true;
  qa_app_configuration_content_destroy(record->content);
  free(record);
}
qa_app_configuration_content *qa_remote_unified_configuration_content(qa_remote_application *app,
    qa_app_loaded_content *content, qa_app_main_mount_lease lease) {
  mounted_record *record = xcalloc(1, sizeof *record);
  record->lease = lease;
  record->content = qa_app_configuration_content_over_loaded(content, mounted_close, record);
  if (record->content == NULL) { free(record); qa_range_error("Out of memory"); }
  qa_remote_arena_add(&app->arena, mounted_drop, record);
  return record->content;
}
