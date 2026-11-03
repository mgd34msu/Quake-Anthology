/* Port of src/app/bootstrap/simulation/bot-assets.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_ASSETS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/bot_selected_knowledge.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/behavior/assets.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
#include "qa/text/localization.h"
/* TS BotAssetFiles.provenance(): SHA-256 hex over (u32le name length, u32le
 * byte length, name, bytes) per file in add order. Writes 65 bytes (hex + NUL). */
void qa_app_sim_bot_asset_provenance(const qa_bot_asset_files *files, char out[65]);
/* TS BotSourceFiles view of a BotAssetFiles (read, list, provenance). read
 * returns a copy valid until the next read; list names are borrowed until
 * the next list; provenance text until the next provenance. */
typedef struct qa_app_sim_bot_asset_view {
  qa_bot_asset_files *files;
  uint8_t *read_buffer;
  const char *names[256];
  char provenance[65];
} qa_app_sim_bot_asset_view;
qa_source_bot_files qa_app_sim_bot_asset_source_files(qa_app_sim_bot_asset_view *view);
void qa_app_sim_bot_asset_view_destroy(qa_app_sim_bot_asset_view *view);
/* TS LoadedApplicationContent as loadApplicationBotAssets reads it. mounted
 * loads content.forContent(id) through loadMountedBotAssetFiles(mounts,
 * catalog) into out (false: OOM). installed is catalog.products.find(
 * expectation.id === id && availability.kind === "installed")?.id (NULL is
 * undefined); product is catalog.product(id).id; open is mounts.open(path)?.bytes
 * on the content's mounts (malloc'd, NULL is TS null). Every member receives ctx. */
typedef struct qa_app_sim_bot_asset_content {
  void *ctx;
  const qa_executable_recipe *recipe;
  bool (*mounted)(void *ctx, qa_content_id content, qa_bot_asset_files *out);
  qa_content_id (*installed)(void *ctx, const char *expectation_id);
  qa_content_id (*product)(void *ctx, const char *id);
  uint8_t *(*open)(void *ctx, qa_content_id content, const char *path, size_t *out_length);
} qa_app_sim_bot_asset_content;
/* TS ApplicationBotAssets: { kind: "q3", files } | { kind: "rerelease",
 * source, files, knowledge, localization }. Heap allocated (views point into
 * it); free with qa_app_sim_application_bot_assets_free. */
typedef struct qa_app_sim_application_bot_assets {
  const char *kind;   /* "q3" | "rerelease" */
  const char *source; /* "q1-rerelease" | "q2-rerelease" (rerelease) */
  qa_bot_asset_files native; /* BotAssetFiles */
  qa_app_sim_bot_asset_view view;
  qa_source_bot_files files; /* view of native */
  qa_bot_knowledge *knowledge; /* rerelease */
  qa_bot_knowledge *arsenal_knowledge; /* selected Q1/Q2 arsenal weapons (borrowed by knowledge) */
  qa_app_sim_q3_weapon_knowledge selected_q3; /* selected Q3 arsenal weapons (borrowed by knowledge) */
  qa_localization_table localization; /* rerelease */
} qa_app_sim_application_bot_assets;
/* TS loadApplicationBotAssets (async becomes synchronous); TS throws raise.
 * Authored map bot definitions win. Classic products without these files
 * keep their existing policy. NULL only on OOM. */
qa_app_sim_application_bot_assets *qa_app_sim_load_application_bot_assets(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation);
/* TS loadApplicationBotAssets(content, simulation, legacyProvenance): with legacy_provenance the native files
 * (and the selected arsenal's provenance note) also carry the version 1 digest a restored bot save checks. */
qa_app_sim_application_bot_assets *qa_app_sim_load_application_bot_assets_with(const qa_app_sim_bot_asset_content *content,
    const qa_app_sim_bot_simulation *simulation, bool legacy_provenance);
void qa_app_sim_application_bot_assets_free(qa_app_sim_application_bot_assets *assets);
#endif
