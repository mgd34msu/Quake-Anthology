/* Port of src/app/bootstrap/simulation/bot-assets.ts. The TS async loads are
 * synchronous (PORTING.md). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/rerelease/data/knowledge_q1.h"
#include "qa/bots/behavior/rerelease/data/knowledge_q2.h"
#include "qa/foundation/support.h"
#include "qa/text/localization_resources.h"

static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }

static void put_u32(uint8_t out[4], uint32_t v) { out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24); }
/* TS BotAssetFiles.provenance(): the bots library digest (it skips paths recorded unread). */
void qa_app_sim_bot_asset_provenance(const qa_bot_asset_files *files, char out[65]) { qa_bot_asset_files_provenance(files, out); }
static const uint8_t *view_read(void *ctx, const char *path, size_t *out_length) {
  qa_app_sim_bot_asset_view *view = ctx;
  int64_t length = qa_bot_asset_files_read(view->files, path, NULL, 0);
  if (length < 0) return NULL;
  free(view->read_buffer);
  view->read_buffer = malloc((size_t)length + 1);
  if (view->read_buffer == NULL) qa_range_error("Out of memory");
  qa_bot_asset_files_read(view->files, path, view->read_buffer, (size_t)length);
  *out_length = (size_t)length;
  return view->read_buffer;
}
static const char *const *view_list(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  qa_app_sim_bot_asset_view *view = ctx;
  *out_count = qa_bot_asset_files_list(view->files, directory, extension, view->names, 256);
  return view->names;
}
static const char *view_provenance(void *ctx) {
  qa_app_sim_bot_asset_view *view = ctx;
  qa_app_sim_bot_asset_provenance(view->files, view->provenance);
  return view->provenance;
}
/* TS BotAssetFiles.legacyProvenance (raises unless prepareLegacyProvenance ran). */
static const char *view_legacy_provenance(void *ctx) {
  return qa_bot_asset_files_legacy_provenance(((qa_app_sim_bot_asset_view *)ctx)->files);
}
qa_source_bot_files qa_app_sim_bot_asset_source_files(qa_app_sim_bot_asset_view *view) {
  qa_source_bot_files files;
  files.files.ctx = view; files.files.read = view_read; files.files.list = view_list; files.provenance = view_provenance;
  files.legacy_provenance = view_legacy_provenance;
  return files;
}
void qa_app_sim_bot_asset_view_destroy(qa_app_sim_bot_asset_view *view) { free(view->read_buffer); view->read_buffer = NULL; }

static bool has_weapons(qa_bot_asset_files *files) { return qa_bot_asset_files_read(files, "bots/weapons.txt", NULL, 0) >= 0; }
static void mounted(const qa_app_sim_bot_asset_content *content, qa_content_id id, qa_bot_asset_files *out) {
  qa_bot_asset_files_init(out);
  if (!content->mounted(content->ctx, id, out)) qa_range_error("Out of memory");
}
typedef struct localization_read { const qa_app_sim_bot_asset_content *content; qa_content_id mounts; qa_bot_asset_files *native; } localization_read;
static uint8_t *read_localization(void *ctx, const char *path, size_t *out_length) {
  localization_read *r = ctx;
  uint8_t *bytes = r->content->open(r->content->ctx, r->mounts, path, out_length);
  if (bytes != NULL && !qa_bot_asset_files_add(r->native, path, bytes, *out_length)) qa_range_error("Out of memory");
  return bytes;
}
static bool load_knowledge(qa_bot_knowledge *out, qa_bot_asset_files *files, bool q1) {
  return q1 ? qa_load_quake1_knowledge(out, files) : qa_bot_load_knowledge(out, files, "PC");
}

static qa_app_sim_application_bot_assets *load_bot_assets(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation, bool legacy_provenance);
qa_app_sim_application_bot_assets *qa_app_sim_load_application_bot_assets(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation) {
  return qa_app_sim_load_application_bot_assets_with(content, simulation, false);
}
qa_app_sim_application_bot_assets *qa_app_sim_load_application_bot_assets_with(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation, bool legacy_provenance) {
  qa_app_sim_application_bot_assets *result = load_bot_assets(content, simulation, legacy_provenance);
  /* if (legacyProvenance && result.files instanceof BotAssetFiles) await result.files.prepareLegacyProvenance() */
  if (result != NULL && legacy_provenance && !qa_bot_asset_files_prepare_legacy_provenance(&result->native)) qa_range_error("Out of memory");
  return result;
}
static qa_app_sim_application_bot_assets *load_bot_assets(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation, bool legacy_provenance) {
  qa_app_sim_application_bot_assets *assets = calloc(1, sizeof *assets);
  if (assets == NULL) return NULL;
  const qa_executable_recipe *recipe = content->recipe;
  qa_content_id mounts = recipe->map.entities.content;
  mounted(content, mounts, &assets->native);
  assets->view.files = &assets->native;
  assets->files = qa_app_sim_bot_asset_source_files(&assets->view);
  if (simulation->q3_source(simulation->ctx) != NULL || simulation->q3_guest(simulation->ctx)) { assets->kind = "q3"; return assets; }
  bool q1 = simulation->q1_source(simulation->ctx) != NULL;
  if (!has_weapons(&assets->native)) {
    qa_content_id sibling = content->installed(content->ctx, q1 ? "q1-rerelease-id1" : "q2-rerelease-baseq2");
    if (sibling != NULL) {
      mounts = sibling;
      qa_bot_asset_files_destroy(&assets->native);
      mounted(content, sibling, &assets->native);
    }
  }
  if (has_weapons(&assets->native)) {
    assets->kind = "rerelease";
    assets->source = q1 ? "q1-rerelease" : "q2-rerelease";
    assets->knowledge = calloc(1, sizeof *assets->knowledge);
    if (assets->knowledge == NULL) qa_range_error("Out of memory");
    if (!load_knowledge(assets->knowledge, &assets->native, q1)) fail("Native bot definitions disappeared during loading");
    if (assets->knowledge->error_count != 0) {
      size_t length = 64;
      for (size_t i = 0; i < assets->knowledge->error_count; i++) length += strlen(assets->knowledge->errors[i]) + 2;
      char *message = malloc(length);
      if (message == NULL) qa_range_error("Out of memory");
      snprintf(message, length, "Invalid native bot definitions: ");
      for (size_t i = 0; i < assets->knowledge->error_count; i++) {
        if (i != 0) strncat(message, "; ", length - strlen(message) - 1);
        strncat(message, assets->knowledge->errors[i], length - strlen(message) - 1);
      }
      qa_raise("Error", 0, 0, "%s", message);
    }
    const qa_provider_reference *selected = recipe->weapons.count != 0 ? &recipe->weapons.values[0] : NULL;
    if (selected != NULL && strcmp(selected->content, recipe->map.entities.content) != 0) {
      qa_bot_asset_files arsenal;
      mounted(content, selected->content, &arsenal);
      if (!starts(selected->provider, "q3:") && !has_weapons(&arsenal)) {
        qa_content_id sibling = content->installed(content->ctx, starts(selected->provider, "q1:") ? "q1-rerelease-id1" : "q2-rerelease-baseq2");
        if (sibling != NULL) { qa_bot_asset_files_destroy(&arsenal); mounted(content, sibling, &arsenal); }
      }
      qa_bot_weapon_rec *weapons = NULL;
      size_t weapon_count = 0;
      if (starts(selected->provider, "q3:")) {
        qa_app_sim_bot_asset_view arsenal_view = {&arsenal, NULL, {0}, {0}};
        qa_source_bot_files arsenal_files = qa_app_sim_bot_asset_source_files(&arsenal_view);
        assets->selected_q3 = qa_app_sim_native_q3_weapon_knowledge(&arsenal_files.files);
        qa_app_sim_bot_asset_view_destroy(&arsenal_view);
        weapons = assets->selected_q3.weapons; weapon_count = assets->selected_q3.count;
      } else {
        bool selected_q1 = starts(selected->provider, "q1:");
        assets->arsenal_knowledge = calloc(1, sizeof *assets->arsenal_knowledge);
        if (assets->arsenal_knowledge == NULL) qa_range_error("Out of memory");
        if (load_knowledge(assets->arsenal_knowledge, &arsenal, selected_q1)) {
          weapons = assets->arsenal_knowledge->weapons; weapon_count = assets->arsenal_knowledge->weapon_count;
        } else { free(assets->arsenal_knowledge); assets->arsenal_knowledge = NULL; }
      }
      if (weapons == NULL || weapon_count == 0) fail("Selected arsenal lacks authored bot weapon knowledge");
      qa_bot_knowledge_replace_weapons(assets->knowledge, weapons, weapon_count);
      /* arsenalNote(digest) = `${provider}\n${content}\n${digest}` */
      if (legacy_provenance && !qa_bot_asset_files_prepare_legacy_provenance(&arsenal)) qa_range_error("Out of memory");
      char provenance[65];
      qa_app_sim_bot_asset_provenance(&arsenal, provenance);
      size_t length = strlen(selected->provider) + strlen(selected->content) + 70;
      char *text = malloc(length), *legacy = NULL;
      if (text == NULL) qa_range_error("Out of memory");
      snprintf(text, length, "%s\n%s\n%s", selected->provider, selected->content, provenance);
      if (legacy_provenance) {
        const char *digest = qa_bot_asset_files_legacy_provenance(&arsenal);
        size_t legacy_length = strlen(selected->provider) + strlen(selected->content) + strlen(digest) + 3;
        legacy = malloc(legacy_length);
        if (legacy == NULL) qa_range_error("Out of memory");
        snprintf(legacy, legacy_length, "%s\n%s\n%s", selected->provider, selected->content, digest);
      }
      bool added = legacy != NULL
          ? qa_bot_asset_files_add_with_legacy(&assets->native, "arsenal/provenance", (const uint8_t *)text, strlen(text), (const uint8_t *)legacy, strlen(legacy))
          : qa_bot_asset_files_add(&assets->native, "arsenal/provenance", (const uint8_t *)text, strlen(text));
      if (!added) qa_range_error("Out of memory");
      free(text); free(legacy);
      qa_bot_asset_files_destroy(&arsenal);
    }
    const qa_app_sim_bot_q1_source *q1_source = simulation->q1_source(simulation->ctx);
    qa_cvar_registry *configuration = q1_source != NULL ? q1_source->cvars : simulation->q2_server_cvars(simulation->ctx);
    char *language = configuration != NULL ? qa_cvar_registry_variable_string(configuration, "language") : NULL;
    localization_read reader = {content, mounts, &assets->native};
    qa_load_server_localization_resources(language != NULL && language[0] != '\0' ? language : "english", read_localization, &reader,
        q1 ? QA_LOC_Q1_RERELEASE : QA_LOC_Q2_RERELEASE, &assets->localization);
    free(language);
    return assets;
  }
  assets->kind = "q3";
  qa_bot_asset_files_destroy(&assets->native);
  mounted(content, content->product(content->ctx, "q3-baseq3"), &assets->native);
  return assets;
}
void qa_app_sim_application_bot_assets_free(qa_app_sim_application_bot_assets *assets) {
  if (assets == NULL) return;
  if (assets->knowledge != NULL) { qa_bot_knowledge_destroy(assets->knowledge); free(assets->knowledge); }
  if (assets->arsenal_knowledge != NULL) { qa_bot_knowledge_destroy(assets->arsenal_knowledge); free(assets->arsenal_knowledge); }
  qa_app_sim_q3_weapon_knowledge_free(&assets->selected_q3);
  if (assets->kind != NULL && strcmp(assets->kind, "rerelease") == 0) qa_localization_table_destroy(&assets->localization);
  qa_app_sim_bot_asset_view_destroy(&assets->view);
  qa_bot_asset_files_destroy(&assets->native);
  free(assets);
}
