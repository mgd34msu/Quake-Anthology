/* Port of src/app/bootstrap/model-loader.ts. Format parsers are
 * landed; converting their outputs into scene DecodedModel values
 * and the md5 pair assembly ride the host below (the same split the
 * landed format headers use). This file owns the replacement
 * selection flow. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MODEL_LOADER_H
#define QA_APP_BOOTSTRAP_MODEL_LOADER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"
/* TS ApplicationModelProvider + format conversion host. */
typedef struct qa_app_model_provider {
  void *ctx;
  qa_game_family family;
  qa_content_mounted *mounts; /* borrowed */
  /* SceneTextureLoader.load(path, {family, usage:"skin"}). */
  void (*load_texture)(void *ctx, const char *path, qa_game_family family);
  /* parseQ12Model / toSceneMd3(parseMd3(...)): fills a decoded model
   * whose storage the host owns until the next call. */
  const qa_decoded_model *(*decode_model)(void *ctx, const uint8_t *bytes, size_t length, const char *path);
  /* MD5 replacement pair assembly: returns false for the TS
   * catch-and-keep-MDL path (ModelTextError/RangeError/BinaryError
   * on skins). On success *out borrows host storage; skins_out lists
   * q1 sidecar skin names / q2 skin paths to texture-load. */
  bool (*md5_replacement)(void *ctx, qa_game_family family,
      const uint8_t *mesh, size_t mesh_length, const char *mesh_path,
      const uint8_t *anim, size_t anim_length, const char *anim_path,
      const uint8_t *scale, size_t scale_length, const char *scale_path,
      const qa_decoded_model *alias, qa_md5_model *out,
      const char *const **skins_out, size_t *skin_count_out, bool *skins_are_sidecars);
  /* decodeQpic validation: false = BinaryError (keep MDL). */
  bool (*validate_qpic)(void *ctx, const uint8_t *bytes, size_t length, const char *path);
} qa_app_model_provider;
/* TS LoadedApplicationModel. */
typedef struct qa_app_loaded_model {
  qa_resolved_resource_reference resource;
  qa_decoded_model model;
  struct qa_app_model_variants *variants; /* owned, NULL = absent */
} qa_app_loaded_model;
typedef struct qa_app_model_variants qa_app_model_variants;
/* TS ApplicationModelVariants over a q1-mdl/q2-md2 decoded model. */
qa_app_model_variants *qa_app_model_variants_create(qa_resolved_resource_reference resource,
    qa_decoded_model native_model);
void qa_app_model_variants_destroy(qa_app_model_variants *variants);
/* The live model view (replacement updates after commit). */
qa_decoded_model qa_app_model_variants_model(const qa_app_model_variants *variants);
/* TS prepareReplacement -> commit closure pair. */
typedef struct qa_app_model_replacement_commit qa_app_model_replacement_commit;
qa_app_model_replacement_commit *qa_app_model_variants_prepare_replacement(
    qa_app_model_variants *variants, const qa_app_model_provider *provider, bool enabled);
void qa_app_model_replacement_commit_apply(qa_app_model_replacement_commit *commit);
void qa_app_model_replacement_commit_destroy(qa_app_model_replacement_commit *commit);
/* TS loadApplicationModel. enhanced_models false disables pairs. */
qa_app_loaded_model qa_app_load_application_model(const qa_app_model_provider *provider,
    const qa_content_opened_resource *asset, bool enhanced_models);
#endif
