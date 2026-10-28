/* Port of src/app/bootstrap/network/unified-content.ts. The landed
 * recipe reader is blocked (persistence/recipe.h), so recipes ride
 * as qa_save_value trees: normalization, mount/resource rebinding,
 * resource-id recomputation and the canonical digest all operate
 * on the tree with TS field names. Catalog/content loading goes
 * through the host below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CONTENT_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/persistence/value.h"
/* TS UnifiedResourceKey (frame-codec owner; defined here once). */
typedef struct qa_app_unified_resource_key {
  qa_content_id content; const char *path; qa_content_digest digest; double byte_length;
} qa_app_unified_resource_key;
typedef struct qa_app_unified_map_sidecar {
  qa_content_id content; const char *path;
  bool has_resource; qa_app_unified_resource_key resource;
} qa_app_unified_map_sidecar;
typedef struct qa_app_unified_composition {
  char *digest;                 /* owned ContentDigest text */
  qa_save_value *recipe;        /* owned normalized recipe tree */
  qa_app_unified_map_sidecar *sidecars; size_t sidecar_count; /* owned */
} qa_app_unified_composition;
/* Malloc'd `resource:unified:<sha256 hex>`; from_reference selects
 * the ResolvedResourceReference overload. */
char *qa_app_unified_resource_id_from_key(const qa_app_unified_resource_key *key);
char *qa_app_unified_resource_id_from_reference(const qa_resolved_resource_reference *reference);
qa_app_unified_resource_key qa_app_unified_resource_key_from_reference(
    const qa_resolved_resource_reference *reference);
qa_app_unified_resource_key qa_app_unified_read_resource_key(const qa_save_reader *reader);
qa_save_value *qa_app_unified_write_resource_key(const qa_app_unified_resource_key *key);
/* sidecars may be NULL/0. Normalized identity is heap-owned. */
qa_app_unified_composition *qa_app_unified_create_composition(
    const qa_save_value *recipe, const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count);
void qa_app_unified_composition_destroy(qa_app_unified_composition *composition);
/* TS buildUnifiedComposition over observed content parts. */
qa_app_unified_composition *qa_app_unified_build_composition(
    const qa_save_value *recipe,
    const qa_app_unified_map_sidecar *sidecars, size_t sidecar_count);
/* TS readUnifiedComposition(unknown): value is the identity tree
 * {digest, composition:{...}}. */
qa_app_unified_composition *qa_app_unified_read_composition(const qa_save_value *value);
uint8_t *qa_app_unified_encode_composition(const qa_save_value *identity_tree, size_t *out_length);
qa_app_unified_composition *qa_app_unified_decode_composition(
    const uint8_t *bytes, size_t length);
/* Host for resolve/load (catalog + mount plan + application content). */
typedef struct qa_app_unified_content_host {
  void *ctx;
  /* catalog.mountsFor(content): candidate mount trees (borrowed). */
  const qa_save_value *const *(*catalog_mounts)(void *ctx, qa_content_id content, size_t *out_count);
  /* openMountPlan read/resolve over the rebound plan tree. */
  void (*plan_read)(void *ctx, const qa_save_value *resource);
  const char *(*plan_resolve_id)(void *ctx, const char *requested_path);
  /* loadApplicationContent + close + observed recipe/sidecars. */
  void *(*load_content)(void *ctx, const qa_save_value *recipe, const char *family);
  void (*close_content)(void *ctx, void *loaded);
  const qa_save_value *(*loaded_recipe)(void *ctx, void *loaded);
  const qa_app_unified_map_sidecar *(*loaded_sidecars)(void *ctx, void *loaded, size_t *out_count);
  const char *(*loaded_family)(void *ctx, void *loaded); /* "q1"|"qw"|"q2"|"q3" */
  const char *(*map_entities_content)(void *ctx, const qa_save_value *recipe);
  const char *(*product_family)(void *ctx, qa_content_id content);
  const char *(*product_edition)(void *ctx, qa_content_id content);
  /* content.forContent(key.content).open(key.path) reference check. */
  bool (*open_resource)(void *ctx, qa_content_id content, const char *path,
      qa_resolved_resource_reference *out);
} qa_app_unified_content_host;
/* Returns the locally rebound recipe tree (owned). */
qa_save_value *qa_app_unified_resolve_composition(
    const qa_save_value *offered_identity, const qa_app_unified_content_host *host);
void *qa_app_unified_load_content(const qa_save_value *identity_tree,
    const qa_app_unified_content_host *host);
qa_resolved_resource_reference qa_app_unified_resolve_resource(
    const qa_app_unified_content_host *host, const qa_app_unified_resource_key *key);
#endif
