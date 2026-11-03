/* Port of src/app/bootstrap/keys.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/keys.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
typedef struct profile_store {
  qa_config_store write;
  qa_config_store fallback;
  bool has_fallback;
  char *loaded; /* borrowed load_text result storage */
  qa_q3_key_storage storage;
} profile_store;
struct qa_app_key_profile {
  qa_cvar_registry *cvars;
  char *game_directory;
  bool demo_restricted;
  qa_q3_cd_key_state state;
  profile_store base;
  profile_store *mod; /* NULL = TS null */
};
struct qa_app_keys {
  qa_app_key_profile *current;
  qa_app_keys_print_fn print;
  void *print_ctx;
  qa_q3_client_authorization *authorization;
  qa_app_key_profile *authorization_profile;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static const char *store_load_text(void *ctx, const char *name) {
  profile_store *store = ctx;
  free(store->loaded); store->loaded = NULL;
  size_t length = 0;
  store->loaded = qa_config_store_load_text(&store->write, name, &length);
  if (store->loaded == NULL && store->has_fallback)
    store->loaded = qa_config_store_load_text(&store->fallback, name, &length);
  return store->loaded;
}
static void store_dump(void *ctx, const char *name, const char *contents) {
  profile_store *store = ctx;
  (void)qa_config_store_dump(&store->write, name, contents);
}
static void store_init(profile_store *store, const qa_app_options *options,
    const qa_catalog_discovered_product *selected) {
  memset(store, 0, sizeof *store);
  char *root = NULL;
  if (selected->user_content != NULL) {
    root = dup_text(selected->user_content->root);
  } else {
    char *fallback_root = NULL;
    const char *user_root = options->user_content_root;
    if (user_root == NULL) { fallback_root = qa_default_user_content_root(); user_root = fallback_root; }
    root = qa_user_product_directory(user_root, selected->expectation.content_directory);
    free(fallback_root);
  }
  if (root == NULL || !qa_config_store_init(&store->write, root)) qa_range_error("Out of memory");
  if (selected->loose_root != NULL && strcmp(selected->loose_root, store->write.root) != 0) {
    if (!qa_config_store_init(&store->fallback, selected->loose_root)) qa_range_error("Out of memory");
    store->has_fallback = true;
  }
  free(root);
  store->storage.ctx = store;
  store->storage.load_text = store_load_text;
  store->storage.dump = store_dump;
}
static void store_destroy(profile_store *store) {
  qa_config_store_destroy(&store->write);
  if (store->has_fallback) qa_config_store_destroy(&store->fallback);
  free(store->loaded);
}
qa_cvar_registry *qa_app_key_profile_cvars(const qa_app_key_profile *profile) { return profile->cvars; }
void qa_app_key_profile_set_cvars(qa_app_key_profile *profile, qa_cvar_registry *cvars) { profile->cvars = cvars; }
const char *qa_app_key_profile_game_directory(const qa_app_key_profile *profile) { return profile->game_directory; }
bool qa_app_key_profile_demo_restricted(const qa_app_key_profile *profile) { return profile->demo_restricted; }
const qa_q3_cd_key_state *qa_app_key_profile_state(const qa_app_key_profile *profile) { return &profile->state; }
void qa_app_key_profile_destroy(qa_app_key_profile *profile) {
  if (profile == NULL) return;
  free(profile->game_directory);
  store_destroy(&profile->base);
  if (profile->mod != NULL) { store_destroy(profile->mod); free(profile->mod); }
  free(profile);
}
void qa_app_key_profile_read_ui(qa_app_key_profile *profile, int64_t unique_key_result,
    const char *game_directory, uint8_t *destination, size_t destination_length,
    const qa_q3_key_ui_writes *writes) {
  qa_q3_cd_key_state_read_ui(&profile->state, unique_key_result, game_directory,
      destination, destination_length, writes);
}
void qa_app_key_profile_write_ui(qa_app_key_profile *profile, double unique,
    const char *directory, const uint8_t *source, size_t source_length) {
  qa_q3_cd_key_state_write_ui(&profile->state, (int64_t)unique, directory, source, source_length);
  bool use_mod = unique == 1 && directory[0] != '\0';
  profile_store *store = use_mod ? profile->mod : &profile->base;
  if (store == NULL) qa_range_error("Selected Q3 UI has no mod key write root");
  qa_q3_cd_key_state_write_file(&profile->state, &store->storage, use_mod ? 16 : 0);
}
void qa_app_key_profile_read_authorization(qa_app_key_profile *profile,
    uint8_t *destination, size_t destination_length) {
  qa_q3_cd_key_state_read_authorization(&profile->state, destination, destination_length);
}
void qa_app_key_profile_save(qa_app_key_profile *profile) {
  qa_q3_cd_key_state_write_file(&profile->state, &profile->base.storage, 0);
  if (profile->mod != NULL)
    qa_q3_cd_key_state_write_file(&profile->state, &profile->mod->storage, 16);
}
typedef struct mark_ctx { qa_app_key_profile *profile; qa_cvar_registry *fallback; } mark_ctx;
static mark_ctx active_mark;
static void mark_modified(void *ctx, int64_t flags) {
  mark_ctx *mark = ctx;
  qa_cvar_registry *cvars = mark->profile != NULL ? mark->profile->cvars : mark->fallback;
  qa_cvar_registry_mark_modified_flags(cvars, (double)flags);
}
static const char *basename_of(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash != NULL ? slash + 1 : path;
}
qa_app_keys *qa_app_keys_create(qa_app_keys_print_fn print, void *print_ctx) {
  qa_app_keys *keys = calloc(1, sizeof *keys);
  if (keys == NULL) qa_range_error("Out of memory");
  keys->print = print; keys->print_ctx = print_ctx;
  return keys;
}
void qa_app_keys_destroy(qa_app_keys *keys) {
  if (keys == NULL) return;
  qa_q3_client_authorization_destroy(keys->authorization);
  qa_app_key_profile_destroy(keys->current);
  free(keys);
}
qa_app_key_profile *qa_app_keys_active(qa_app_keys *keys) {
  if (keys->current == NULL) qa_range_error("Q3 keys have no published source profile");
  return keys->current;
}
static bool demo_restricted_of(void *ctx) {
  qa_app_keys *keys = ctx;
  return qa_app_keys_active(keys)->demo_restricted;
}
qa_q3_client_authorization *qa_app_keys_authorization(qa_app_keys *keys) {
  qa_app_key_profile *active = qa_app_keys_active(keys);
  if (keys->authorization != NULL && keys->authorization_profile == active) return keys->authorization;
  qa_q3_client_authorization_destroy(keys->authorization);
  keys->authorization = NULL; keys->authorization_profile = NULL;
  qa_q3_client_authorization_options options = {0};
  options.cvars = active->cvars;
  options.keys = &active->state;
  options.demo_restricted = demo_restricted_of;
  options.print = (qa_q3_message_print_fn)keys->print;
  options.print_ctx = keys->print_ctx;
  options.ctx = keys;
  if (!qa_q3_client_authorization_init(&keys->authorization, &options)) qa_range_error("Out of memory");
  keys->authorization_profile = active;
  return keys->authorization;
}
qa_app_key_profile *qa_app_keys_prepare(qa_app_keys *keys, const qa_app_options *options,
    const qa_content_catalog *catalog, qa_cvar_registry *cvars) {
  (void)keys;
  bool demo_restricted = options->has_q3_product && options->q3_product.restriction.kind == QA_Q3_MOUNT_DEMO;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options->product);
  if (product->expectation.family != QA_GAME_FAMILY_Q3) return NULL;
  const qa_catalog_discovered_product *base = product;
  while (base->expectation.base_product != NULL)
    base = qa_content_catalog_product(catalog, base->expectation.base_product);
  qa_app_key_profile *profile = calloc(1, sizeof *profile);
  if (profile == NULL) qa_range_error("Out of memory");
  store_init(&profile->base, options, base);
  bool has_mod = strcmp(product->id, base->id) != 0;
  if (has_mod) {
    profile->mod = calloc(1, sizeof *profile->mod);
    if (profile->mod == NULL) qa_range_error("Out of memory");
    store_init(profile->mod, options, product);
  }
  active_mark.profile = NULL; active_mark.fallback = cvars;
  qa_q3_cd_key_state_init(&profile->state, mark_modified, &active_mark, QA_Q3_KEY_BUILD_CLIENT);
  qa_q3_cd_key_state_read_file(&profile->state, &profile->base.storage);
  if (profile->mod != NULL) qa_q3_cd_key_state_append_file(&profile->state, &profile->mod->storage);
  profile->cvars = cvars;
  profile->game_directory = dup_text(has_mod ? basename_of(product->expectation.content_directory) : "");
  profile->demo_restricted = demo_restricted;
  active_mark.profile = profile;
  return profile;
}
void qa_app_keys_publish(qa_app_keys *keys, qa_app_key_profile *profile, qa_cvar_registry *cvars) {
  if (profile != NULL) profile->cvars = cvars;
  qa_q3_client_authorization_destroy(keys->authorization);
  keys->authorization = NULL; keys->authorization_profile = NULL;
  /* Publishing the current profile again keeps it (TS keeps the object the map holds). */
  if (keys->current != profile) qa_app_key_profile_destroy(keys->current);
  keys->current = profile;
}
void qa_app_keys_save(qa_app_keys *keys) {
  if (keys->current != NULL) qa_app_key_profile_save(keys->current);
}
