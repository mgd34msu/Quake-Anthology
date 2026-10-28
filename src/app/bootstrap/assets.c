/* Port of src/app/bootstrap/assets.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/assets.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct { qa_content_id content; qa_app_provider_scene_assets provider; } provider_row;
struct qa_app_application_assets {
  qa_app_loaded_content *content;
  qa_app_assets_host host;
  bool has_image_policy; qa_app_image_policy image_policy;
  qa_app_model_policy model_policy;
  const void *world, *font, *typography;
  provider_row *providers; size_t provider_count, provider_cap;
  bool closed;
};
static void phase(const char *error) { if (error) qa_range_error("%s", error); }
qa_app_application_assets *qa_app_application_assets_create(qa_app_loaded_content *content, qa_app_assets_host host, qa_app_model_policy model_policy) {
  qa_app_application_assets *assets = calloc(1, sizeof *assets);
  if (!assets) return NULL;
  assets->content = content;
  assets->host = host;
  assets->model_policy = model_policy;
  return assets;
}
void qa_app_application_assets_destroy(qa_app_application_assets *assets) {
  if (!assets) return;
  qa_app_application_assets_close(assets);
  free(assets->providers);
  free(assets);
}
qa_app_loaded_content *qa_app_application_assets_content(const qa_app_application_assets *a) { return a->content; }
bool qa_app_application_assets_image_policy(const qa_app_application_assets *a, qa_app_image_policy *out) {
  if (!a->has_image_policy) return false;
  *out = a->image_policy;
  return true;
}
void qa_app_application_assets_set_image_policy(qa_app_application_assets *a, qa_app_image_policy policy) {
  a->has_image_policy = true;
  a->image_policy = policy;
}
qa_app_model_policy qa_app_application_assets_model_policy(const qa_app_application_assets *a) { return a->model_policy; }
void qa_app_application_assets_set_model_policy(qa_app_application_assets *a, qa_app_model_policy policy) { a->model_policy = policy; }
const void *qa_app_application_assets_world(qa_app_application_assets *a) {
  if (!a->world) qa_range_error("World presentation has not loaded");
  return a->world;
}
void qa_app_application_assets_set_world(qa_app_application_assets *a, const void *world) { a->world = world; }
qa_app_provider_scene_assets qa_app_application_assets_provider(qa_app_application_assets *a, qa_content_id content) {
  if (a->closed) qa_range_error("Application assets are closed");
  for (size_t i = 0; i < a->provider_count; i++)
    if (strcmp(a->providers[i].content, content) == 0) return a->providers[i].provider;
  qa_app_provider_scene_assets provider = {0};
  phase(a->host.provider(a->host.ctx, a, content, &provider));
  if (a->provider_count == a->provider_cap) {
    a->provider_cap = a->provider_cap ? a->provider_cap * 2 : 8;
    a->providers = realloc(a->providers, a->provider_cap * sizeof *a->providers);
  }
  a->providers[a->provider_count++] = (provider_row){content, provider};
  return provider;
}
const void *qa_app_application_assets_load_world(qa_app_application_assets *a) {
  const void *world = NULL;
  phase(a->host.load_world(a->host.ctx, a, &world));
  a->world = world;
  return world;
}
const void *qa_app_application_assets_load_console_font(qa_app_application_assets *a) {
  if (a->font) return a->font;
  phase(a->host.load_font(a->host.ctx, a, &a->font));
  return a->font;
}
const void *qa_app_application_assets_load_menu_typography(qa_app_application_assets *a) {
  if (a->typography) return a->typography;
  phase(a->host.load_typography(a->host.ctx, a, &a->typography));
  return a->typography;
}
const void *qa_app_application_assets_prepare_image_refresh(qa_app_application_assets *a, qa_app_image_policy policy, bool has_model_policy, qa_app_model_policy model_policy) {
  const void *prepared = NULL;
  phase(a->host.prepare_refresh(a->host.ctx, a, policy, has_model_policy ? model_policy : a->model_policy, &prepared));
  return prepared;
}
void qa_app_application_assets_commit_image_refresh(qa_app_application_assets *a, const void *prepared) { a->host.commit_refresh(a->host.ctx, prepared); }
void qa_app_application_assets_discard_image_refresh(qa_app_application_assets *a, const void *prepared) { a->host.discard_refresh(a->host.ctx, prepared); }
void qa_app_application_assets_finish_image_refresh(qa_app_application_assets *a) { a->host.finish_refresh(a->host.ctx, a); }
qa_app_model_asset qa_app_application_assets_model(qa_app_application_assets *a, qa_content_id content, const char *path) {
  qa_app_model_asset asset = {0};
  phase(a->host.model(a->host.ctx, a, content, path, &asset));
  return asset;
}
void qa_app_application_assets_close(qa_app_application_assets *a) {
  if (!a || a->closed) return;
  a->closed = true;
  qa_app_application_assets_finish_image_refresh(a);
  a->host.close_assets(a->host.ctx, a);
  a->world = NULL;
  a->font = NULL;
  a->typography = NULL;
  a->provider_count = 0;
}
