/* Port of src/app/bootstrap/content.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/content.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_product.h"
#include "qa/compat/qvm/compatibility.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/products.h"
#include "qa/content/catalog/source_program.h"
#include "qa/content/catalog/timing.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/content/user_data.h"
#include "qa/formats/bsp_kind.h"
#include "qa/formats/q1_map.h"
#include "qa/formats/q2_map.h"
#include "qa/formats/q3_map/world.h"
#include "qa/foundation/support.h"
bool qa_app_application_discovers_mods(const qa_app_options *options, bool has_recipe,
                                       bool product_known, qa_game_family product_family) {
  /* TS: weaponBehavior !== undefined || mods || dedicated || offline && (...) */
  if (options->has_weapon_behavior || options->mod_count > 0 || options->dedicated) return true;
  return options->network_kind && strcmp(options->network_kind, "offline") == 0 &&
      (has_recipe || !product_known || product_family == QA_GAME_FAMILY_Q3);
}
qa_app_application_source_selection qa_app_application_source_selection_of(qa_app_content_host host, const char *product, const char *rules_option) {
  qa_game_family family = QA_GAME_FAMILY_Q1;
  const char *edition = NULL, *campaign = NULL;
  bool installed = false, builtin = false;
  if (!host.require_product(host.ctx, product)) qa_range_error("Unknown content product %s", product);
  host.product(host.ctx, product, &family, &edition, &campaign, &installed, &builtin);
  const char *family_text = family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
  bool rerelease = edition && strcmp(edition, "rerelease") == 0;
  const char *rules = rules_option;
  if (!rules) {
    if (family == QA_GAME_FAMILY_Q2 && campaign && (strcmp(campaign, "ctf") == 0 || strcmp(campaign, "lmctf") == 0)) rules = campaign;
    else if (family == QA_GAME_FAMILY_Q3 && campaign && (strcmp(campaign, "ctf") == 0 || strcmp(campaign, "lrctf") == 0)) rules = "ctf";
    else rules = "standard";
  }
  qa_app_application_source_selection out = {0};
  static char source_provider[32], match_provider[64], match_content[64];
  snprintf(source_provider, sizeof source_provider, "%s:official", family_text);
  out.source.provider = source_provider;
  out.source.content = product;
  out.rules = rules;
  bool guest_mod = family == QA_GAME_FAMILY_Q3 && !builtin;
  if (strcmp(rules, "standard") == 0 || guest_mod) out.match = out.source;
  else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q3) {
    out.match.provider = "q3:ctf"; out.match.content = product;
  } else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q1) {
    snprintf(match_content, sizeof match_content, "q1-%s-ctf", rerelease ? "rerelease" : "classic");
    if (!host.require_product(host.ctx, match_content)) qa_range_error("Unknown content product %s", match_content);
    out.match.provider = "q1:ctf"; out.match.content = match_content;
  } else if (strcmp(rules, "ctf") == 0 || strcmp(rules, "lmctf") == 0) {
    snprintf(match_provider, sizeof match_provider, "q2:%s", rules);
    snprintf(match_content, sizeof match_content, "q2-%s-%s", rerelease ? "rerelease" : "classic", rules);
    if (!host.require_product(host.ctx, match_content)) qa_range_error("Unknown content product %s", match_content);
    out.match.provider = match_provider; out.match.content = match_content;
  } else if (strcmp(rules, "tag") == 0 || strcmp(rules, "deathball") == 0) {
    snprintf(match_provider, sizeof match_provider, "q2:%s", rules);
    snprintf(match_content, sizeof match_content, "q2-%s-rogue", rerelease ? "rerelease" : "classic");
    if (!host.require_product(host.ctx, match_content)) qa_range_error("Unknown content product %s", match_content);
    out.match.provider = match_provider; out.match.content = match_content;
  } else {
    snprintf(match_provider, sizeof match_provider, "%s:%s", family_text, rules);
    out.match.provider = match_provider; out.match.content = product;
  }
  return out;
}
struct qa_app_loaded_content {
  qa_app_content_host host;
  qa_app_options options;
  qa_executable_recipe recipe; bool has_recipe;
  const void *catalog, *mounts;
  bool has_q3_policy; qa_q3_product_policy q3_policy;
  const void *opened[16]; size_t opened_count;
  bool closed;
  /* Native (concrete loader) storage; NULL on the host path. */
  qa_content_catalog *native_catalog; bool owns_catalog;
  qa_content_mounted *native_mounts;
  qa_resolve_launch_result *launch_result;
  qa_app_application_preset_result *preset_result;
  const char *world_kind; void *world; int world_family; /* 1|2|3 */
  qa_app_application_map_sidecar *sidecars; size_t sidecar_count;
  qa_content_mounted **native_opened; size_t native_opened_count, native_opened_cap;
  struct { char *content; qa_content_mounted *mounts; } *scoped; size_t scoped_count, scoped_cap;
  bool has_pure; qa_pure_mount_policy pure;
  const qa_q3_application_product *native_q3_product;
  size_t main_mount_leases;
  qa_resolved_resource_reference *sidecar_refs; size_t sidecar_ref_count;
  qa_resolved_resource_reference *owned_resources;
  qa_launch_string_list native_strings;
};
struct qa_app_configuration_content {
  qa_app_content_host host;
  const void *catalog, *mounts, *q3_product;
  qa_app_application_configuration_selection selection;
  bool closed;
};
static void phase(const char *error) { if (error) qa_range_error("%s", error); }
qa_app_loaded_content *qa_app_load_application_content(qa_app_content_host host, qa_app_options options,
                                                       const qa_executable_recipe *restored_recipe, bool has_recipe) {
  qa_app_loaded_content *content = calloc(1, sizeof *content);
  if (!content) return NULL;
  content->host = host;
  content->options = options;
  content->has_recipe = has_recipe;
  if (has_recipe && restored_recipe) content->recipe = *restored_recipe;
  if (host.open_loaded) phase(host.open_loaded(host.ctx, content));
  return content;
}
static void native_world_destroy(qa_app_loaded_content *c) {
  if (!c->world) return;
  if (c->world_family == 1) qa_q1_map_destroy((qa_q1_map *)c->world);
  else if (c->world_family == 2) qa_q2_decoded_map_destroy((qa_q2_decoded_map *)c->world);
  else if (c->world_family == 3) qa_q3_decoded_world_destroy((qa_q3_decoded_world *)c->world);
  free(c->world); c->world = NULL;
}
void qa_app_loaded_content_destroy(qa_app_loaded_content *content) {
  if (!content) return;
  qa_app_loaded_content_close(content);
  for (size_t i = 0; i < content->scoped_count; i++) qa_content_mounted_destroy(content->scoped[i].mounts);
  if (content->native_mounts) qa_content_mounted_destroy(content->native_mounts);
  native_world_destroy(content);
  for (size_t i = 0; i < content->sidecar_count; i++) free((void *)content->sidecars[i].path);
  free(content->sidecars); free(content->sidecar_refs); free(content->owned_resources);
  qa_launch_string_list_free(&content->native_strings);
  for (size_t i = 0; i < content->scoped_count; i++) free(content->scoped[i].content);
  free(content->scoped); free(content->native_opened);
  if (content->launch_result) { qa_resolve_launch_result_free(content->launch_result); free(content->launch_result); }
  if (content->preset_result) { qa_app_application_preset_result_free(content->preset_result); free(content->preset_result); }
  if (content->owns_catalog && content->native_catalog) { qa_content_catalog_destroy(content->native_catalog); free(content->native_catalog); }
  qa_app_options_destroy(&content->options);
  free(content);
}
const qa_executable_recipe *qa_app_loaded_content_recipe(const qa_app_loaded_content *c) { return &c->recipe; }
void qa_app_loaded_content_set_recipe(qa_app_loaded_content *c, qa_executable_recipe recipe) { c->recipe = recipe; c->has_recipe = true; }
const void *qa_app_loaded_content_catalog(const qa_app_loaded_content *c) { return c->catalog; }
void qa_app_loaded_content_set_catalog(qa_app_loaded_content *c, const void *catalog) { c->catalog = catalog; }
const void *qa_app_loaded_content_mounts(const qa_app_loaded_content *c) { return c->mounts; }
void qa_app_loaded_content_set_mounts(qa_app_loaded_content *c, const void *mounts) { c->mounts = mounts; }
bool qa_app_loaded_content_q3_policy(const qa_app_loaded_content *c, qa_q3_product_policy *out) {
  if (!c->has_q3_policy) return false;
  *out = c->q3_policy;
  return true;
}
void qa_app_loaded_content_set_q3_policy(qa_app_loaded_content *c, qa_q3_product_policy policy) { c->has_q3_policy = true; c->q3_policy = policy; }
static qa_content_mounted *native_for_content(qa_app_loaded_content *c, qa_content_id content_id);
const void *qa_app_loaded_content_for_content(qa_app_loaded_content *c, qa_content_id content_id) {
  if (c->native_catalog) return native_for_content(c, content_id);
  const void *mounts = NULL;
  phase(c->host.for_content(c->host.ctx, c, content_id, &mounts));
  if (mounts && c->opened_count < 16) c->opened[c->opened_count++] = mounts;
  return mounts;
}
size_t qa_app_loaded_content_opened_mounts(qa_app_loaded_content *c, const void **out, size_t capacity) {
  if (c->closed) return 0;
  size_t count = 0;
  const void *main_mounts = c->native_mounts ? (const void *)c->native_mounts : c->mounts;
  if (main_mounts && count < capacity) out[count++] = main_mounts;
  for (size_t i = 0; i < c->native_opened_count && count < capacity; i++) out[count++] = c->native_opened[i];
  for (size_t i = 0; i < c->opened_count && count < capacity; i++) out[count++] = c->opened[i];
  return count;
}
void qa_app_loaded_content_prepare_mods(qa_app_loaded_content *c, const char *purpose) {
  phase(c->host.prepare_mods(c->host.ctx, c, purpose ? purpose : "gameplay"));
}
void qa_app_loaded_content_prepare_qvm_grapple(qa_app_loaded_content *c) { phase(c->host.prepare_grapple(c->host.ctx, c)); }
void qa_app_loaded_content_prepare_weapon_behaviors(qa_app_loaded_content *c) { phase(c->host.prepare_weapon_behaviors(c->host.ctx, c)); }
void qa_app_loaded_content_close(qa_app_loaded_content *c) {
  if (!c || c->closed) return;
  c->closed = true;
  if (c->native_mounts && c->main_mount_leases == 0) qa_content_mounted_close(c->native_mounts);
  for (size_t i = 0; i < c->native_opened_count; i++) qa_content_mounted_close(c->native_opened[i]);
  c->native_opened_count = 0;
  if (c->host.close_loaded) phase(c->host.close_loaded(c->host.ctx, c));
}
qa_executable_recipe qa_app_resolve_application_travel(qa_app_loaded_content *c, const char *path) {
  qa_executable_recipe out = {0};
  phase(c->host.resolve_travel(c->host.ctx, c, path, &out));
  return out;
}
qa_app_configuration_content *qa_app_open_application_configuration_content(qa_app_content_host host, qa_app_configuration_request request) {
  (void)request;
  qa_app_configuration_content *content = calloc(1, sizeof *content);
  if (!content) return NULL;
  content->host = host;
  if (host.open_configuration) phase(host.open_configuration(host.ctx, content));
  return content;
}
qa_app_configuration_content *qa_app_open_remote_application_content(qa_app_content_host host, qa_app_options options) {
  (void)options;
  qa_app_configuration_content *content = calloc(1, sizeof *content);
  if (!content) return NULL;
  content->host = host;
  if (host.open_remote) phase(host.open_remote(host.ctx, content));
  return content;
}
void qa_app_configuration_content_destroy(qa_app_configuration_content *c) {
  if (!c) return;
  qa_app_configuration_content_close(c);
  free(c);
}
qa_app_application_configuration_selection qa_app_configuration_content_selection(const qa_app_configuration_content *c) { return c->selection; }
void qa_app_configuration_content_set_selection(qa_app_configuration_content *c, qa_app_application_configuration_selection s) { c->selection = s; }
const void *qa_app_configuration_content_mounts(const qa_app_configuration_content *c) { return c->mounts; }
void qa_app_configuration_content_set_mounts(qa_app_configuration_content *c, const void *m) { c->mounts = m; }
const void *qa_app_configuration_content_catalog(const qa_app_configuration_content *c) { return c->catalog; }
void qa_app_configuration_content_set_catalog(qa_app_configuration_content *c, const void *catalog) { c->catalog = catalog; }
bool qa_app_configuration_content_q3_product(const qa_app_configuration_content *c, const void **out) {
  *out = c->q3_product;
  return c->q3_product != NULL;
}
void qa_app_configuration_content_set_q3_product(qa_app_configuration_content *c, const void *p) { c->q3_product = p; }
void qa_app_configuration_content_close(qa_app_configuration_content *c) { if (c) c->closed = true; }
qa_launch_preset qa_app_application_preset(qa_app_content_host host, const qa_app_options *options) {
  qa_launch_preset out = {0};
  phase(host.preset(host.ctx, options, false, &out));
  return out;
}
qa_launch_preset qa_app_application_configuration_preset(qa_app_content_host host, const qa_app_options *options) {
  qa_launch_preset out = {0};
  phase(host.preset(host.ctx, options, true, &out));
  return out;
}
/* --- Concrete composition below (TS content.ts). --- */
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, text, length);
  return copy;
}
static char *xformat(const char *format, const char *a, const char *b) {
  int length = snprintf(NULL, 0, format, a, b);
  if (length < 0) return NULL;
  char *out = malloc((size_t)length + 1);
  if (out) snprintf(out, (size_t)length + 1, format, a, b);
  return out;
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
}
static bool is_expected_product(const char *id) {
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, id) == 0) return true;
  return false;
}
static const qa_catalog_product_expectation *find_expected(const char *id) {
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, id) == 0) return &qa_expected_products[i];
  return NULL;
}
static const char *base_product_id(qa_game_family family) {
  switch (family) {
    case QA_GAME_FAMILY_Q1: return "q1-classic-id1";
    case QA_GAME_FAMILY_Q2: return "q2-classic-baseq2";
    case QA_GAME_FAMILY_Q3: return "q3-baseq3";
  }
  return "q1-classic-id1";
}
bool qa_app_application_discovers_mods_native(const qa_app_options *options, bool has_recipe) {
  const qa_catalog_product_expectation *product = options->product ? find_expected(options->product) : NULL;
  return options->has_weapon_behavior || options->mod_count > 0 || options->dedicated ||
      (options->network_kind && strcmp(options->network_kind, "offline") == 0 &&
       (has_recipe || product == NULL || product->family == QA_GAME_FAMILY_Q3));
}
void qa_app_source_selection_result_free(qa_app_source_selection_result *result) {
  free(result->source_provider); free(result->match_provider);
  memset(result, 0, sizeof *result);
}
bool qa_app_application_source_selection_for_catalog(const qa_content_catalog *catalog,
    const char *product_id, const char *rules_option, qa_app_source_selection_result *out) {
  memset(out, 0, sizeof *out);
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, product_id);
  qa_game_family family = product->expectation.family;
  const qa_catalog_product_expectation *program = &qa_source_program_product(catalog, product->id)->expectation;
  bool rerelease = program->edition && strcmp(program->edition, "rerelease") == 0;
  const char *rules = rules_option;
  if (!rules) {
    if (family == QA_GAME_FAMILY_Q2 && program->campaign &&
        (strcmp(program->campaign, "ctf") == 0 || strcmp(program->campaign, "lmctf") == 0)) rules = program->campaign;
    else if (family == QA_GAME_FAMILY_Q3 && program->campaign &&
        (strcmp(program->campaign, "ctf") == 0 || strcmp(program->campaign, "lrctf") == 0)) rules = "ctf";
    else rules = "standard";
  }
  out->source_provider = xformat("%s:official", family_name(family), NULL);
  if (!out->source_provider) return false;
  out->selection.source.provider = out->source_provider;
  out->selection.source.content = product->id;
  out->selection.rules = rules;
  bool guest_mod = family == QA_GAME_FAMILY_Q3 && !is_expected_product(program->id);
  if (strcmp(rules, "standard") == 0 || guest_mod) {
    out->selection.match = out->selection.source;
  } else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q3) {
    out->selection.match.provider = "q3:ctf"; out->selection.match.content = product->id;
  } else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q1) {
    char wanted[64]; snprintf(wanted, sizeof wanted, "q1-%s-ctf", rerelease ? "rerelease" : "classic");
    out->selection.match.provider = "q1:ctf";
    out->selection.match.content = qa_content_catalog_require(catalog, wanted)->id;
  } else if (strcmp(rules, "ctf") == 0 || strcmp(rules, "lmctf") == 0) {
    char wanted[64]; snprintf(wanted, sizeof wanted, "q2-%s-%s", rerelease ? "rerelease" : "classic", rules);
    out->match_provider = xformat("q2:%s", rules, NULL);
    if (!out->match_provider) { qa_app_source_selection_result_free(out); return false; }
    out->selection.match.provider = out->match_provider;
    out->selection.match.content = qa_content_catalog_require(catalog, wanted)->id;
  } else if (strcmp(rules, "tag") == 0 || strcmp(rules, "deathball") == 0) {
    char wanted[64]; snprintf(wanted, sizeof wanted, "q2-%s-rogue", rerelease ? "rerelease" : "classic");
    out->match_provider = xformat("q2:%s", rules, NULL);
    if (!out->match_provider) { qa_app_source_selection_result_free(out); return false; }
    out->selection.match.provider = out->match_provider;
    out->selection.match.content = qa_content_catalog_require(catalog, wanted)->id;
  } else {
    out->match_provider = xformat("%s:%s", family_name(family), rules);
    if (!out->match_provider) { qa_app_source_selection_result_free(out); return false; }
    out->selection.match.provider = out->match_provider;
    out->selection.match.content = product->id;
  }
  return true;
}
const qa_catalog_discovered_product *qa_app_movement_content_product(const qa_content_catalog *catalog, const char *movement_product) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, movement_product);
  if (strcmp(product->expectation.id, "q1-quakeworld") == 0 && product->availability.kind != QA_CATALOG_INSTALLED)
    return qa_content_catalog_require(catalog, product->expectation.base_product ? product->expectation.base_product : "q1-classic-id1");
  return qa_content_catalog_require(catalog, movement_product);
}
static bool list_contains(const char *const *values, size_t count, const char *wanted) {
  for (size_t i = 0; i < count; i++) if (strcmp(values[i], wanted) == 0) return true;
  return false;
}
static const char *selected_q2_game_library(const qa_content_catalog *catalog, const qa_app_options *options) {
  if (options->q2_game_library) return options->q2_game_library;
  const qa_catalog_discovered_product *product = qa_source_program_product(catalog, options->product);
  const char *library = product->expectation.edition && strcmp(product->expectation.edition, "rerelease") == 0 ? "game_x64.dll" : "gamex86.dll";
  return product->expectation.family == QA_GAME_FAMILY_Q2 && !is_expected_product(product->expectation.id) &&
      list_contains(product->expectation.required_programs, product->expectation.required_program_count, library) ? library : NULL;
}
static const char *selected_quakec_program(const qa_content_catalog *catalog, const qa_app_options *options) {
  if (options->quakec_program) return options->quakec_program;
  const qa_catalog_discovered_product *product = qa_source_program_product(catalog, options->product);
  return product->expectation.family == QA_GAME_FAMILY_Q1 &&
      !(product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0) &&
      !is_expected_product(product->expectation.id) &&
      list_contains(product->expectation.required_programs, product->expectation.required_program_count, "progs.dat") ? "progs.dat" : NULL;
}
static bool network_is(const qa_app_options *options, const char *kind) {
  return options->network_kind && strcmp(options->network_kind, kind) == 0;
}
typedef struct player_products { const char *movement, *character; } player_products;
static player_products application_player_products(const qa_content_catalog *catalog, const qa_app_options *options, bool has_native_source, bool native_source) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options->product);
  qa_game_family family = product->expectation.family;
  bool own = has_native_source ? native_source :
      product->availability.kind == QA_CATALOG_INSTALLED &&
      ((product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0 && !network_is(options, "qw-client")) ||
       (family == QA_GAME_FAMILY_Q3 && !network_is(options, "q3-client") &&
        !is_expected_product(qa_source_program_product(catalog, product->id)->expectation.id)));
  player_products out;
  out.movement = options->movement_product ? qa_app_movement_content_product(catalog, options->movement_product)->expectation.id
      : own && options->movement == family ? product->expectation.id : base_product_id(options->movement);
  out.character = own && options->character == family ? product->expectation.id : base_product_id(options->character);
  return out;
}
static qa_execution_selection typescript_execution(qa_provider_reference provider, qa_game_family family, bool rerelease, qa_provider_id implementation) {
  qa_execution_selection out; memset(&out, 0, sizeof out);
  out.kind = QA_EXECUTION_MODULE_TYPESCRIPT; out.owner = provider; out.role = "server-game";
  out.implementation = implementation ? implementation : provider.provider;
  switch (family) {
    case QA_GAME_FAMILY_Q1:
      out.api.kind = QA_GAME_API_QUAKEC; out.api.as.quakec.kind = QA_QUAKEC_API_Q1_NETQUAKE;
      out.api.as.quakec.program_version = 6; out.api.as.quakec.system_crc = 5927; break;
    case QA_GAME_FAMILY_Q2:
      out.api.kind = QA_GAME_API_Q2_GAME;
      out.api.as.q2_game.kind = rerelease ? QA_Q2_GAME_API_RERELEASE : QA_Q2_GAME_API_CLASSIC;
      out.api.as.q2_game.version = rerelease ? 2023 : 3; break;
    case QA_GAME_FAMILY_Q3:
      out.api.kind = QA_GAME_API_Q3; out.api.as.q3.kind = QA_Q3_API_QAGAME; out.api.as.q3.version = 8; break;
  }
  return out;
}
static bool preset_remember(qa_app_application_preset_result *result, char *text) {
  if (!text) return false;
  if (result->string_count == result->string_cap) {
    size_t cap = result->string_cap ? result->string_cap * 2 : 8;
    char **strings = realloc(result->strings, cap * sizeof *strings);
    if (!strings) { free(text); return false; }
    result->strings = strings; result->string_cap = cap;
  }
  result->strings[result->string_count++] = text;
  return true;
}
void qa_app_application_preset_result_free(qa_app_application_preset_result *result) {
  for (size_t i = 0; i < 3; i++)
    if (result->timing_storage[i].provider) qa_native_provider_timing_free(&result->timing_storage[i]);
  for (size_t i = 0; i < result->string_count; i++) free(result->strings[i]);
  free(result->strings);
  /* resolved_options is a shallow borrow of the caller's options. */
  memset(result, 0, sizeof *result);
}
static bool provider_timing_for(const qa_content_catalog *catalog, const qa_app_options *options,
    const qa_app_native_provider_sources *native_sources, qa_provider_reference reference,
    qa_game_family source, bool edition_rerelease, bool movement_role, qa_provider_timing *out) {
  if (!qa_native_provider_timing(reference, source, edition_rerelease, out)) return false;
  bool quakeworld = source == QA_GAME_FAMILY_Q1 &&
      (strcmp(qa_content_catalog_product(catalog, reference.content)->expectation.edition ?
              qa_content_catalog_product(catalog, reference.content)->expectation.edition : "", "quakeworld") == 0 ||
       (movement_role && options->movement_product && strcmp(options->movement_product, "q1-quakeworld") == 0));
  if (quakeworld) {
    out->clock.kind = QA_CLOCK_Q1_QUAKEWORLD;
    out->clock.maximum_command_milliseconds =
        movement_role && (options->movement_product != NULL || native_sources != NULL) ? 255 : 50;
  }
  return true;
}
static bool contains_ci(const char *text, const char *needle) {
  size_t n = strlen(needle);
  for (size_t i = 0; text[i]; i++) {
    size_t j = 0;
    while (j < n && text[i + j] && tolower((unsigned char)text[i + j]) == tolower((unsigned char)needle[j])) j++;
    if (j == n) return true;
  }
  return n == 0;
}
static bool selected_application_preset(const qa_content_catalog *catalog, const qa_app_options *options,
    const qa_app_native_provider_sources *native_sources, bool quakeworld, bool q3_guest,
    qa_app_application_preset_result *out) {
  memset(out, 0, sizeof *out);
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, options->product);
  qa_game_family family = product->expectation.family;
  const char *native_program = selected_quakec_program(catalog, options);
  const char *q2_library = selected_q2_game_library(catalog, options);
  qa_app_source_selection_result selection;
  if (!qa_app_application_source_selection_for_catalog(catalog, options->product, options->rules, &selection)) return false;
  if (!preset_remember(out, selection.source_provider)) { free(selection.match_provider); return false; }
  selection.source_provider = NULL;
  if (selection.match_provider && !preset_remember(out, selection.match_provider)) { qa_app_application_preset_result_free(out); return false; }
  selection.match_provider = NULL;
  qa_provider_reference provider = selection.selection.source, match = selection.selection.match;
  const char *rules = selection.selection.rules;
  const qa_catalog_discovered_product *program_product =
      strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q1 && !quakeworld
          ? qa_content_catalog_require(catalog, match.content) : qa_source_program_product(catalog, product->id);
  qa_provider_reference equipment_source = provider;
  if (!native_program && !q2_library && !q3_guest && !quakeworld) equipment_source.content = program_product->id;
  player_products players = application_player_products(catalog, options, true, quakeworld || q3_guest);
  qa_provider_reference movement, character;
  if (native_sources) { movement = native_sources->movement; character = native_sources->character; }
  else {
    char *movement_provider = xformat("%s:movement", family_name(options->movement), NULL);
    char *character_provider = xformat("%s:character", family_name(options->character), NULL);
    if (!preset_remember(out, movement_provider) || !preset_remember(out, character_provider)) { qa_app_application_preset_result_free(out); return false; }
    movement = (qa_provider_reference){out->strings[out->string_count - 2], qa_content_catalog_require(catalog, players.movement)->id};
    character = (qa_provider_reference){out->strings[out->string_count - 1], qa_content_catalog_require(catalog, players.character)->id};
  }
  char *appearance_provider = xformat("%s:model/%s", family_name(options->character), options->character_model ? options->character_model : "");
  if (!preset_remember(out, appearance_provider)) { qa_app_application_preset_result_free(out); return false; }
  qa_provider_reference appearance = {out->strings[out->string_count - 1], character.content};
  bool rerelease = q2_library && family != QA_GAME_FAMILY_Q2 ? contains_ci(q2_library, "x64")
      : product->expectation.edition && strcmp(product->expectation.edition, "rerelease") == 0;
  qa_launch_preset *preset = &out->preset;
  char revision[512];
  snprintf(revision, sizeof revision, "%s-%s-%s-%s%s%s", options->product,
      options->movement_product ? options->movement_product : family_name(options->movement),
      family_name(options->character), options->character_model ? options->character_model : "",
      strcmp(rules, "standard") == 0 ? "" : "-", strcmp(rules, "standard") == 0 ? "" : rules);
  char *preset_id = qa_create_recipe_id("mixed", revision);
  if (!preset_remember(out, preset_id)) { qa_app_application_preset_result_free(out); return false; }
  preset->id = out->strings[out->string_count - 1];
  preset->map.geometry = (qa_resource_request){options->map_product ? qa_content_catalog_require(catalog, options->map_product)->id : product->id, options->map};
  preset->map.entities = provider;
  bool deathmatch = options->mode && strcmp(options->mode, "deathmatch") == 0;
  preset->campaign = (qa_campaign_selection){!deathmatch, provider, provider};
  preset->movement = movement;
  preset->character = (qa_character_selection){character, appearance};
  out->weapons_storage[0] = provider;
  preset->weapons = (qa_provider_reference_list){out->weapons_storage, 1};
  preset->equipment = qa_native_equipment(catalog, equipment_source, match);
  preset->enemies = (qa_enemy_selection){QA_ENEMY_SELECTION_MAP_DEFINED, {0}, NULL, 0, {NULL, NULL}};
  preset->presentation = (qa_presentation_selection){{true}, {QA_ENVIRONMENT_AUDIO_CONTENT, {NULL, NULL}}, product->id, provider, provider, provider};
  preset->engine_behavior = provider; preset->combat = provider; preset->inventory = provider;
  preset->match = match; preset->transition = provider;
  qa_execution_selection *execution = &out->execution_storage[0];
  if (q2_library) {
    memset(execution, 0, sizeof *execution);
    execution->kind = QA_EXECUTION_MODULE_NATIVE; execution->owner = provider; execution->role = "server-game";
    out->artifact_storage[0] = (qa_resource_request){product->id, q2_library};
    execution->artifact = &out->artifact_storage[0];
    execution->api.kind = QA_GAME_API_Q2_GAME;
    if (rerelease) {
      execution->api.as.q2_game = (qa_q2_game_api_identity){QA_Q2_GAME_API_RERELEASE, 2023};
      execution->profile = (qa_native_abi){"windows-x86-64", "pe32+", 8, "microsoft-x64"};
    } else {
      execution->api.as.q2_game = (qa_q2_game_api_identity){QA_Q2_GAME_API_CLASSIC, 3};
      execution->profile = (qa_native_abi){"windows-i386", "pe32", 4, "cdecl"};
    }
  } else if (q3_guest) {
    memset(execution, 0, sizeof *execution);
    execution->kind = QA_EXECUTION_MODULE_QVM; execution->owner = provider; execution->role = "server-game";
    out->artifact_storage[0] = (qa_resource_request){product->id, "vm/qagame.qvm"};
    execution->artifact = &out->artifact_storage[0];
    execution->api.kind = QA_GAME_API_Q3; execution->api.as.q3 = (qa_q3_api_identity){QA_Q3_API_QAGAME, 8};
  } else if (quakeworld) {
    memset(execution, 0, sizeof *execution);
    execution->kind = QA_EXECUTION_MODULE_QUAKEC; execution->owner = provider; execution->role = "server-game";
    out->artifact_storage[0] = (qa_resource_request){product->id, "qwprogs.dat"};
    execution->artifact = &out->artifact_storage[0];
    execution->api.kind = QA_GAME_API_QUAKEC; execution->api.as.quakec.kind = QA_QUAKEC_API_Q1_QUAKEWORLD;
    execution->api.as.quakec.program_version = 6; execution->api.as.quakec.system_crc = 54730;
  } else if (native_program) {
    memset(execution, 0, sizeof *execution);
    execution->kind = QA_EXECUTION_MODULE_QUAKEC; execution->owner = provider; execution->role = "server-game";
    out->artifact_storage[0] = (qa_resource_request){product->id, native_program};
    execution->artifact = &out->artifact_storage[0];
    execution->api.kind = QA_GAME_API_QUAKEC; execution->api.as.quakec.kind = QA_QUAKEC_API_Q1_NETQUAKE;
    execution->api.as.quakec.program_version = 6; execution->api.as.quakec.system_crc = 5927;
  } else {
    qa_provider_id implementation = provider.provider;
    char *program_implementation = NULL;
    if (strcmp(program_product->id, product->id) != 0) {
      program_implementation = qa_source_program_implementation(&program_product->expectation);
      if (!preset_remember(out, program_implementation)) { qa_app_application_preset_result_free(out); return false; }
      implementation = out->strings[out->string_count - 1];
    }
    *execution = typescript_execution(provider, family, rerelease, implementation);
  }
  preset->execution = out->execution_storage; preset->execution_count = 1;
  if (!provider_timing_for(catalog, options, native_sources, provider, family, rerelease, false, &out->timing_storage[0]) ||
      !provider_timing_for(catalog, options, native_sources, movement, options->movement,
          qa_content_catalog_product(catalog, movement.content)->expectation.edition &&
          strcmp(qa_content_catalog_product(catalog, movement.content)->expectation.edition, "rerelease") == 0, true, &out->timing_storage[1]) ||
      !provider_timing_for(catalog, options, native_sources, character, options->character,
          qa_content_catalog_product(catalog, character.content)->expectation.edition &&
          strcmp(qa_content_catalog_product(catalog, character.content)->expectation.edition, "rerelease") == 0, false, &out->timing_storage[2])) {
    qa_app_application_preset_result_free(out); return false;
  }
  preset->timing = out->timing_storage; preset->timing_count = 3;
  out->ordering_storage[0] = provider.provider; out->ordering_storage[1] = movement.provider; out->ordering_storage[2] = character.provider;
  preset->ordering = (qa_frame_ordering){QA_FRAME_ORDERING_MIXED, {0}, out->ordering_storage, 3};
  return true;
}
static void resolve_movement(const qa_content_catalog *catalog, qa_app_application_preset_result *out, const qa_app_options *options) {
  out->resolved_options = *options; out->has_resolved_options = true;
  if (!options->movement_product) return;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options->movement_product);
  qa_app_movement_content_product(catalog, options->movement_product);
  out->resolved_options.movement = product->expectation.family;
  out->resolved_options.movement_product = (char *)product->expectation.id;
}
bool qa_app_application_preset_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options_in,
    const qa_app_native_provider_sources *native_sources, const qa_app_content_source *presentation_source,
    qa_app_application_preset_result *out) {
  memset(out, 0, sizeof *out);
  resolve_movement(catalog, out, options_in);
  const qa_app_options *options = &out->resolved_options;
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, options->product);
  qa_game_family family = product->expectation.family;
  const char *presentation_family = presentation_source ? NULL : NULL; (void)presentation_family;
  bool q3_guest = family == QA_GAME_FAMILY_Q3 && !(presentation_source && presentation_source->family == QA_APP_DEMO_Q3) &&
      !network_is(options, "q3-client") && !is_expected_product(qa_source_program_product(catalog, product->id)->expectation.id);
  if (q3_guest && !network_is(options, "native-server") && !network_is(options, "offline"))
    qa_range_error("Selected Q3 mods require an offline local or dedicated server");
  bool quakeworld = product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0 &&
      !(presentation_source && presentation_source->family == QA_APP_DEMO_QW) && !network_is(options, "qw-client");
  const char *native_program = selected_quakec_program(catalog, options);
  if (selected_q2_game_library(catalog, options) && (options->quakec_program || options->has_bot_skill ||
      (!network_is(options, "offline") && !network_is(options, "native-server") && !network_is(options, "q2-server"))))
    qa_range_error("--q2-game requires native offline/server operation without a second guest program");
  if (native_program && ((product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0) ||
      (!network_is(options, "offline") && !network_is(options, "native-server")) ||
      (network_is(options, "native-server") && options->character != QA_GAME_FAMILY_Q1)))
    qa_range_error("--progs requires an offline/server NetQuake source world; native wire servers require Q1 character presentation");
  if (quakeworld && network_is(options, "native-server") && (!options->dedicated ||
      !(options->mode && strcmp(options->mode, "deathmatch") == 0) || options->character != QA_GAME_FAMILY_Q1 || options->has_q1_protocol))
    qa_range_error("Native QuakeWorld requires dedicated deathmatch with Q1 character presentation and its native protocol");
  qa_app_options resolved = out->resolved_options;
  options = &resolved;
  if (!selected_application_preset(catalog, options, native_sources, quakeworld, q3_guest, out)) return false;
  out->resolved_options = resolved; out->has_resolved_options = true;
  return true;
}
bool qa_app_application_configuration_preset_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options_in,
    const qa_app_native_provider_sources *native_sources, qa_app_application_preset_result *out) {
  memset(out, 0, sizeof *out);
  resolve_movement(catalog, out, options_in);
  const qa_app_options *options = &out->resolved_options;
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, options->product);
  qa_game_family family = product->expectation.family;
  bool q3_guest = family == QA_GAME_FAMILY_Q3 && !is_expected_product(qa_source_program_product(catalog, product->id)->expectation.id);
  bool quakeworld = product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0;
  qa_app_options resolved = out->resolved_options;
  options = &resolved;
  if (!selected_application_preset(catalog, options, native_sources, quakeworld, q3_guest, out)) return false;
  out->resolved_options = resolved; out->has_resolved_options = true;
  return true;
}
static bool inspect_archive(void *ctx, const char *path, qa_archive_format format, qa_catalog_archive *out, char **error_out) {
  (void)ctx; (void)error_out;
  qa_archive_handle *handle = qa_open_archive(path, true, format);
  if (!handle) return false;
  memset(out, 0, sizeof *out);
  out->path = xstrdup(path); out->format = handle->format;
  out->entries = calloc(handle->entry_count ? handle->entry_count : 1, sizeof *out->entries);
  if (!out->path || !out->entries) { free(out->path); free(out->entries); qa_archive_destroy(handle); return false; }
  for (size_t i = 0; i < handle->entry_count; i++) {
    out->entries[i].path = xstrdup(handle->entries[i].path);
    out->entries[i].ordinal = handle->entries[i].ordinal;
    out->entries[i].byte_length = handle->entries[i].byte_length;
    if (!out->entries[i].path) { out->entry_count = i; qa_catalog_archive_free(out); qa_archive_destroy(handle); return false; }
  }
  out->entry_count = handle->entry_count;
  qa_archive_destroy(handle);
  return true;
}
qa_catalog_archive_inspector qa_app_default_archive_inspector(void) {
  qa_catalog_archive_inspector inspector = {NULL, inspect_archive};
  return inspector;
}
/* qa_launch_mounted over qa_content_mounted (launch.ts services). */
typedef struct launch_adapter {
  qa_content_mounted *mounted; bool owns_mounted;
  qa_mounted_content_reader *reader;
  uint8_t *last_bytes;
} launch_adapter;
static bool launch_adapter_open(void *ctx, const char *path, qa_launch_mount_predicate accept, void *accept_ctx, qa_catalog_opened_resource *out) {
  launch_adapter *adapter = ctx;
  qa_content_opened_resource opened;
  bool found = adapter->reader
      ? qa_mounted_content_reader_open(adapter->reader, path, (qa_mount_accept_fn)accept, accept_ctx, &opened)
      : qa_content_mounted_open(adapter->mounted, path, (qa_mount_accept_fn)accept, accept_ctx, &opened);
  if (!found) return false;
  free(adapter->last_bytes); adapter->last_bytes = opened.bytes;
  out->reference = *opened.reference; out->bytes = opened.bytes; out->byte_length = opened.byte_length;
  return true;
}
static bool launch_adapter_resolve(void *ctx, const char *path, qa_resolved_resource_reference *out) {
  launch_adapter *adapter = ctx;
  const qa_resolved_resource_reference *reference = NULL;
  bool found = adapter->reader ? qa_mounted_content_reader_resolve(adapter->reader, path, &reference)
                               : qa_content_mounted_resolve(adapter->mounted, path, &reference);
  if (!found) return false;
  *out = *reference;
  return true;
}
static void launch_adapter_close(void *ctx);
static bool launch_adapter_borrow(void *ctx, qa_mount_plan_id id, const qa_mount_id *order, size_t order_count, qa_launch_mounted *out) {
  launch_adapter *adapter = ctx;
  qa_mounted_content_reader *reader = qa_content_mounted_borrow_ordered_reader(adapter->mounted, id, order, order_count, NULL, 0);
  if (!reader) return false;
  launch_adapter *scoped = calloc(1, sizeof *scoped);
  if (!scoped) { qa_mounted_content_reader_destroy(reader); return false; }
  scoped->mounted = adapter->mounted; scoped->reader = reader;
  memset(out, 0, sizeof *out);
  out->ctx = scoped; out->plan = qa_mounted_content_reader_plan(reader);
  out->open = launch_adapter_open; out->resolve = launch_adapter_resolve;
  out->borrow_ordered = launch_adapter_borrow; out->close = launch_adapter_close;
  return true;
}
static void launch_adapter_close(void *ctx) {
  launch_adapter *adapter = ctx;
  if (adapter->reader) qa_mounted_content_reader_destroy(adapter->reader);
  if (adapter->owns_mounted && adapter->mounted) qa_content_mounted_destroy(adapter->mounted);
  free(adapter->last_bytes); free(adapter);
}
static bool launch_service_open_plan(void *ctx, const qa_resolved_mount_plan *plan, const void *options, qa_launch_mounted *out) {
  (void)ctx;
  qa_content_mounted *mounted = qa_open_mount_plan(plan, (const qa_open_mount_options *)options);
  if (!mounted) return false;
  launch_adapter *adapter = calloc(1, sizeof *adapter);
  if (!adapter) { qa_content_mounted_destroy(mounted); return false; }
  adapter->mounted = mounted; adapter->owns_mounted = true;
  memset(out, 0, sizeof *out);
  out->ctx = adapter; out->plan = qa_content_mounted_plan(mounted);
  out->open = launch_adapter_open; out->resolve = launch_adapter_resolve;
  out->borrow_ordered = launch_adapter_borrow; out->close = launch_adapter_close;
  return true;
}
static const char *launch_service_qvm_compat(void *ctx, const qa_launch_mounted *mounted, const char *artifact_path, qa_content_digest digest, const char *role) {
  (void)ctx;
  launch_adapter *adapter = mounted->ctx;
  qa_qvm_abi_profile profile = NULL;
  qa_qvm_module_path_digest module = {artifact_path, digest};
  if (!qa_qvm_read_compatibility(adapter->mounted, module, role, &profile)) return NULL;
  return profile;
}
static qa_launch_services native_launch_services(void) {
  qa_launch_services services = {NULL, launch_service_open_plan, launch_service_qvm_compat};
  return services;
}
static char *hex_of(const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = malloc(length * 2 + 1);
  if (!out) return NULL;
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 15];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 15];
  }
  out[length * 2] = '\0';
  return out;
}
static bool mount_id_equal(qa_mount_id a, qa_mount_id b) { return a && b && strcmp(a, b) == 0; }
static qa_content_mounted *native_for_content(qa_app_loaded_content *c, qa_content_id content_id) {
  if (c->closed) qa_range_error("Application content is closed");
  for (size_t i = 0; i < c->scoped_count; i++)
    if (strcmp(c->scoped[i].content, content_id) == 0) return c->scoped[i].mounts;
  qa_catalog_mount_list primary = {0}, rules = {0};
  if (!qa_content_catalog_mounts_for(c->native_catalog, content_id, &primary)) qa_range_error("Out of memory");
  bool want_rules = c->has_recipe && strcmp(content_id, c->recipe.map.entities.content) == 0 &&
      strcmp(c->recipe.match.content, content_id) != 0;
  if (want_rules && !qa_content_catalog_mounts_for(c->native_catalog, c->recipe.match.content, &rules)) {
    qa_content_catalog_mount_list_free(&primary); qa_range_error("Out of memory");
  }
  size_t total = primary.count + rules.count;
  qa_content_mount *mounts = calloc(total ? total : 1, sizeof *mounts);
  qa_mount_id *order = calloc(total ? total : 1, sizeof *order);
  if (!mounts || !order) { free(mounts); free(order); qa_content_catalog_mount_list_free(&primary); qa_content_catalog_mount_list_free(&rules); qa_range_error("Out of memory"); }
  size_t count = 0;
  for (int pass = 0; pass < 2; pass++) {
    qa_catalog_mount_list *list = pass == 0 ? &primary : &rules;
    for (size_t i = 0; i < list->count; i++) {
      qa_mount_identity identity = list->mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? list->mounts[i].as.archive.identity : list->mounts[i].as.loose.identity;
      bool seen = false;
      for (size_t j = 0; j < count; j++) if (mount_id_equal(order[j], identity.id)) seen = true;
      if (!seen) { mounts[count] = list->mounts[i]; order[count] = identity.id; count++; }
    }
  }
  qa_open_mount_options mount_options; memset(&mount_options, 0, sizeof mount_options);
  if (c->has_pure) {
    size_t kept = 0;
    for (size_t i = 0; i < c->pure.archive_count; i++)
      for (size_t j = 0; j < count; j++)
        if (mounts[j].kind == QA_CONTENT_MOUNT_ARCHIVE && mounts[j].as.archive.archive_digest &&
            strcmp(mounts[j].as.archive.archive_digest, c->pure.archives[i]) == 0) kept++;
    if (c->pure.archive_count > 0 && kept == 0) {
      free(mounts); free(order); qa_content_catalog_mount_list_free(&primary); qa_content_catalog_mount_list_free(&rules);
      qa_range_error("No server-approved archives provide %s", content_id);
    }
    mount_options.has_pure = true; mount_options.pure = c->pure;
  }
  char *hex = hex_of(content_id);
  char *plan_id = hex ? qa_create_mount_plan_id("provider", hex) : NULL;
  free(hex);
  qa_resolved_mount_plan plan = {plan_id, mounts, count, order, count, NULL, 0};
  qa_content_mounted *opened = c->native_mounts ? qa_content_mounted_borrow_mount_plan(c->native_mounts, &plan, &mount_options) : NULL;
  if (!opened) opened = qa_open_mount_plan(&plan, &mount_options);
  /* Plan strings/mounts are borrowed by the opened plan copy from the
   * catalog mount lists; those lists are intentionally retained for
   * the content lifetime (freed with the catalog generation in TS). */
  free(mounts); free(order); free(plan_id);
  (void)primary; (void)rules;
  if (!opened) qa_range_error("Out of memory");
  if (c->closed) { qa_content_mounted_destroy(opened); qa_range_error("Application content closed during mount"); }
  if (c->scoped_count == c->scoped_cap) {
    c->scoped_cap = c->scoped_cap ? c->scoped_cap * 2 : 4;
    void *grown = realloc(c->scoped, c->scoped_cap * sizeof *c->scoped);
    if (!grown) qa_range_error("Out of memory");
    c->scoped = grown;
  }
  if (c->native_opened_count == c->native_opened_cap) {
    c->native_opened_cap = c->native_opened_cap ? c->native_opened_cap * 2 : 4;
    void *grown = realloc(c->native_opened, c->native_opened_cap * sizeof *c->native_opened);
    if (!grown) qa_range_error("Out of memory");
    c->native_opened = grown;
  }
  char *key = xstrdup(content_id);
  if (!key) qa_range_error("Out of memory");
  c->scoped[c->scoped_count].content = key; c->scoped[c->scoped_count].mounts = opened; c->scoped_count++;
  c->native_opened[c->native_opened_count++] = opened;
  return opened;
}
qa_app_application_world qa_app_loaded_content_world(const qa_app_loaded_content *c) {
  qa_app_application_world world = {c->world_kind, c->world};
  if (c->world_family == 3 && c->world) world.value = &((qa_q3_decoded_world *)c->world)->world;
  return world;
}
const qa_app_application_map_sidecar *qa_app_loaded_content_map_sidecars(const qa_app_loaded_content *c, size_t *out_count) {
  *out_count = c->sidecar_count;
  return c->sidecars;
}
typedef struct mount_lease { qa_app_loaded_content *content; bool released; } mount_lease;
static void mount_lease_release(void *ctx) {
  mount_lease *lease = ctx;
  if (lease->released) return;
  lease->released = true;
  qa_app_loaded_content *c = lease->content;
  c->main_mount_leases--;
  if (c->closed && c->main_mount_leases == 0 && c->native_mounts) qa_content_mounted_close(c->native_mounts);
  free(lease);
}
qa_app_main_mount_lease qa_app_loaded_content_retain_main_mounts(qa_app_loaded_content *c) {
  if (c->closed) qa_range_error("Application content is closed");
  mount_lease *lease = calloc(1, sizeof *lease);
  if (!lease) qa_range_error("Out of memory");
  lease->content = c;
  c->main_mount_leases++;
  qa_app_main_mount_lease out = {lease, mount_lease_release};
  return out;
}
static void sidecar_push(qa_app_loaded_content *c, qa_content_id content, char *path, const qa_resolved_resource_reference *reference) {
  qa_app_application_map_sidecar *sidecars = realloc(c->sidecars, (c->sidecar_count + 1) * sizeof *sidecars);
  if (!sidecars) qa_range_error("Out of memory");
  c->sidecars = sidecars;
  const qa_resolved_resource_reference *stored = NULL;
  if (reference) {
    qa_resolved_resource_reference *old_base = c->sidecar_refs;
    qa_resolved_resource_reference *refs = realloc(c->sidecar_refs, (c->sidecar_ref_count + 1) * sizeof *refs);
    if (!refs) qa_range_error("Out of memory");
    if (refs != old_base && old_base != NULL)
      for (size_t i = 0; i < c->sidecar_count; i++)
        if (c->sidecars[i].resource != NULL)
          c->sidecars[i].resource = refs + (c->sidecars[i].resource - old_base);
    c->sidecar_refs = refs;
    c->sidecar_refs[c->sidecar_ref_count] = *reference;
    stored = &c->sidecar_refs[c->sidecar_ref_count++];
  }
  c->sidecars[c->sidecar_count++] = (qa_app_application_map_sidecar){content, path, stored};
}
static int sidecar_compare(const void *a, const void *b) {
  return strcmp(((const qa_app_application_map_sidecar *)a)->path, ((const qa_app_application_map_sidecar *)b)->path);
}
static char *replace_bsp_suffix(const char *path, const char *suffix) {
  size_t length = strlen(path);
  if (length >= 4 && strcmp(path + length - 4, ".bsp") == 0) {
    char *out = malloc(length - 4 + strlen(suffix) + 1);
    if (!out) return NULL;
    memcpy(out, path, length - 4); strcpy(out + length - 4, suffix);
    return out;
  }
  return xstrdup(path);
}
static qa_content_mounted *open_map_content(qa_content_catalog *catalog, const qa_executable_recipe *recipe, bool demo) {
  qa_catalog_mount_list list = {0};
  if (!qa_content_catalog_mounts_for(catalog, recipe->map.geometry_content, &list)) return NULL;
  qa_mount_id *order = calloc(list.count ? list.count : 1, sizeof *order);
  if (!order) { qa_content_catalog_mount_list_free(&list); return NULL; }
  for (size_t i = 0; i < list.count; i++)
    order[i] = list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? list.mounts[i].as.archive.identity.id : list.mounts[i].as.loose.identity.id;
  char *hex = hex_of(recipe->map.geometry_content);
  char *plan_id = hex ? qa_create_mount_plan_id("map-sidecars", hex) : NULL;
  free(hex);
  qa_resolved_mount_plan plan = {plan_id, list.mounts, list.count, order, list.count, NULL, 0};
  qa_open_mount_options options; memset(&options, 0, sizeof options); options.q3_restriction_demo = demo;
  qa_content_mounted *opened = qa_open_mount_plan(&plan, &options);
  free(order); free(plan_id);
  /* list backing is borrowed by the opened plan for its lifetime. */
  (void)list;
  return opened;
}
typedef struct material_stash { char **paths; uint8_t **bytes; size_t *lengths; size_t count; } material_stash;
static uint8_t *stash_read_material(void *ctx, const char *path, size_t *out_length) {
  material_stash *stash = ctx;
  for (size_t i = 0; i < stash->count; i++) {
    if (strcmp(stash->paths[i], path) != 0 || !stash->bytes[i]) continue;
    uint8_t *copy = malloc(stash->lengths[i] ? stash->lengths[i] : 1);
    if (!copy) return NULL;
    memcpy(copy, stash->bytes[i], stash->lengths[i]);
    *out_length = stash->lengths[i];
    return copy;
  }
  return NULL;
}
qa_app_loaded_content *qa_app_load_application_content_native(qa_app_content_host guest_host, qa_app_options options,
    const qa_executable_recipe *restored_recipe, bool has_recipe, const qa_pure_mount_policy *pure,
    qa_content_catalog *installed_catalog, const qa_app_content_source *presentation_source) {
  if (options.has_remote_content) {
    const char *base = options.remote_content.base;
    const char *network = strcmp(base, "q1-quakeworld") == 0 ? "qw-client" : strcmp(base, "q2-classic-baseq2") == 0 ? "q2-client" : "q3-client";
    qa_app_demo_family recorded = strcmp(base, "q1-quakeworld") == 0 ? QA_APP_DEMO_QW : strcmp(base, "q2-classic-baseq2") == 0 ? QA_APP_DEMO_Q2 : QA_APP_DEMO_Q3;
    char *remote_product = qa_remote_content_product(&options.remote_content, NULL, 0);
    bool mismatch = presentation_source == NULL ? !network_is(&options, network) : presentation_source->family != recorded;
    if (mismatch || remote_product == NULL || options.product == NULL || strcmp(options.product, remote_product) != 0) {
      free(remote_product);
      qa_range_error("Remote content context requires its matching remote client product");
    }
    free(remote_product);
  }
  qa_app_loaded_content *loaded = calloc(1, sizeof *loaded);
  if (!loaded) { qa_app_options_destroy(&options); return NULL; }
  loaded->host = guest_host; loaded->options = options; loaded->has_recipe = has_recipe;
  if (pure) { loaded->has_pure = true; loaded->pure = *pure; }
  qa_content_catalog *catalog = installed_catalog;
  char *default_user_root = NULL;
  qa_nightdive_addon_root *default_addons = NULL; size_t default_addon_count = 0;
  qa_catalog_nightdive_addon_root *addon_roots = NULL;
  if (catalog == NULL) {
    catalog = calloc(1, sizeof *catalog);
    if (!catalog) { qa_app_loaded_content_destroy(loaded); return NULL; }
    loaded->native_catalog = catalog; loaded->owns_catalog = true;
    const char *user_root = options.user_content_root;
    if (!user_root) { default_user_root = qa_default_user_content_root(); user_root = default_user_root; }
    qa_discover_content_options discover; memset(&discover, 0, sizeof discover);
    discover.corpus_root = options.corpus_root; discover.user_content_root = user_root;
    discover.discover_mods = qa_app_application_discovers_mods_native(&options, has_recipe);
    discover.inspector = qa_app_default_archive_inspector();
    if (options.addon_content_root_count > 0) {
      addon_roots = calloc(options.addon_content_root_count, sizeof *addon_roots);
      if (!addon_roots) { free(default_user_root); qa_app_loaded_content_destroy(loaded); return NULL; }
      for (size_t i = 0; i < options.addon_content_root_count; i++)
        addon_roots[i] = (qa_catalog_nightdive_addon_root){options.addon_content_roots[i].family, options.addon_content_roots[i].root};
      discover.addon_content_roots = addon_roots; discover.addon_content_root_count = options.addon_content_root_count;
    } else if (qa_default_addon_content_roots(NULL, &default_addons, &default_addon_count)) {
      addon_roots = calloc(default_addon_count ? default_addon_count : 1, sizeof *addon_roots);
      if (!addon_roots) { qa_nightdive_addon_roots_free(default_addons, default_addon_count); free(default_user_root); qa_app_loaded_content_destroy(loaded); return NULL; }
      for (size_t i = 0; i < default_addon_count; i++)
        addon_roots[i] = (qa_catalog_nightdive_addon_root){default_addons[i].family, default_addons[i].root};
      discover.addon_content_roots = addon_roots; discover.addon_content_root_count = default_addon_count;
    }
    if (options.has_remote_content) discover.remote_content = &options.remote_content;
    bool discovered = qa_discover_installed_content(&discover, catalog);
    free(addon_roots); qa_nightdive_addon_roots_free(default_addons, default_addon_count); free(default_user_root);
    if (!discovered) { qa_app_loaded_content_destroy(loaded); return NULL; }
  } else {
    loaded->native_catalog = catalog;
  }
  /* prepareQ3ApplicationProduct: the non-Q3 and unrestricted-Q3 cases
   * are concrete here; demo-restricted catalog construction needs the
   * q3-product slice's restrict_catalog host (fail closed, no stub). */
  const char *q3_content = has_recipe && restored_recipe ? restored_recipe->map.entities.content : options.product;
  const qa_catalog_discovered_product *q3_selected = qa_content_catalog_product(catalog, q3_content);
  const qa_q3_application_product *q3_product = NULL;
  if (q3_selected->expectation.family == QA_GAME_FAMILY_Q3) {
    if (!options.has_q3_product || options.q3_product.restriction.kind != QA_Q3_MOUNT_NONE) {
      qa_app_loaded_content_destroy(loaded);
      return NULL;
    }
    q3_product = &loaded->options.q3_product;
    loaded->has_q3_policy = true; loaded->q3_policy = options.q3_product.policy;
  }
  loaded->native_q3_product = q3_product;
  loaded->catalog = catalog;
  qa_open_mount_options mount_options; memset(&mount_options, 0, sizeof mount_options);
  if (pure) { mount_options.has_pure = true; mount_options.pure = *pure; }
  if (presentation_source != NULL) {
    qa_game_family wanted = presentation_source->family == QA_APP_DEMO_QW ? QA_GAME_FAMILY_Q1
        : presentation_source->family == QA_APP_DEMO_Q1 ? QA_GAME_FAMILY_Q1
        : presentation_source->family == QA_APP_DEMO_Q2 ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q3;
    if (qa_content_catalog_require(catalog, options.product)->expectation.family != wanted)
      qa_range_error("Recorded content family differs from the selected product");
  }
  qa_executable_recipe recipe;
  if (has_recipe && restored_recipe) {
    recipe = *restored_recipe;
  } else {
    loaded->preset_result = calloc(1, sizeof *loaded->preset_result);
    loaded->launch_result = calloc(1, sizeof *loaded->launch_result);
    if (!loaded->preset_result || !loaded->launch_result) { qa_app_loaded_content_destroy(loaded); return NULL; }
    if (!qa_app_application_preset_for_catalog(catalog, &loaded->options, NULL, presentation_source, loaded->preset_result)) {
      qa_app_loaded_content_destroy(loaded); return NULL;
    }
    qa_resolve_launch_options launch; memset(&launch, 0, sizeof launch);
    launch.choice = qa_preset_choice(loaded->preset_result->preset.id);
    launch.preset = &loaded->preset_result->preset;
    launch.catalog = catalog; launch.mounts_options = &mount_options; launch.services = native_launch_services();
    if (!qa_resolve_launch(&launch, loaded->launch_result)) { qa_app_loaded_content_destroy(loaded); return NULL; }
    recipe = loaded->launch_result->recipe;
    if (presentation_source == NULL && loaded->options.has_weapon_behavior) {
      /* prepareConfiguredApplicationRecipe needs the weapon-behavior
       * slice host; fail closed rather than skip it silently. */
      qa_app_loaded_content_destroy(loaded);
      return NULL;
    }
  }
  loaded->recipe = recipe; loaded->has_recipe = true;
  bool unified = presentation_source != NULL && presentation_source->kind && strcmp(presentation_source->kind, "unified") == 0;
  bool needs_guest = false;
  for (size_t i = 0; i < recipe.execution_count; i++) {
    const qa_resolved_execution_module *module = &recipe.execution[i];
    if (unified) continue;
    if (module->kind == QA_EXECUTION_MODULE_QVM && module->role && strcmp(module->role, "server-game") == 0) {
      if (!network_is(&options, "native-server") && !network_is(&options, "offline"))
        qa_range_error("Q3 bytecode requires offline local or dedicated Q3 server operation");
      needs_guest = true;
      continue;
    }
    if (module->kind == QA_EXECUTION_MODULE_QUAKEC) {
      const qa_catalog_discovered_product *entities_product = qa_content_catalog_product(catalog, recipe.map.entities.content);
      bool qw_api = module->api.kind == QA_GAME_API_QUAKEC && module->api.as.quakec.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
      bool nq_api = module->api.kind == QA_GAME_API_QUAKEC && module->api.as.quakec.kind == QA_QUAKEC_API_Q1_NETQUAKE;
      bool native_qw = entities_product->expectation.edition && strcmp(entities_product->expectation.edition, "quakeworld") == 0 && qw_api &&
          options.mode && strcmp(options.mode, "deathmatch") == 0 &&
          (network_is(&options, "offline") || network_is(&options, "native-server")) && !options.has_q1_protocol;
      bool native_nq = entities_product->expectation.family == QA_GAME_FAMILY_Q1 && nq_api &&
          (network_is(&options, "offline") || network_is(&options, "native-server"));
      if ((!native_qw && !native_nq) ||
          (!network_is(&options, "offline") && strcmp(recipe.map.geometry_content, recipe.map.entities.content) != 0) ||
          strcmp(module->owner.provider, recipe.map.entities.provider) != 0 ||
          strcmp(module->owner.content, recipe.map.entities.content) != 0 || recipe.execution_count != 1)
        qa_range_error("QuakeC requires matching source actors and validated artifact; native network maps must belong to the source content and QuakeWorld requires dedicated operation");
      needs_guest = true;
      continue;
    }
    if (module->kind == QA_EXECUTION_MODULE_NATIVE && module->role && strcmp(module->role, "server-game") == 0 &&
        ((module->api.kind == QA_GAME_API_Q2_GAME && module->api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC && module->profile.kind && strcmp(module->profile.kind, "windows-i386") == 0) ||
         (module->api.kind == QA_GAME_API_Q2_GAME && module->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE && module->profile.kind && strcmp(module->profile.kind, "windows-x86-64") == 0))) {
      const qa_catalog_discovered_product *entities_product = qa_content_catalog_product(catalog, recipe.map.entities.content);
      bool rerelease_api = module->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE;
      if (entities_product->expectation.family != QA_GAME_FAMILY_Q2 ||
          strcmp(entities_product->expectation.edition ? entities_product->expectation.edition : "", rerelease_api ? "rerelease" : "classic") != 0 ||
          recipe.execution_count != 1 || strcmp(module->owner.provider, recipe.map.entities.provider) != 0 ||
          strcmp(module->owner.content, recipe.map.entities.content) != 0)
        qa_range_error("Native Quake II requires edition-matching source actors");
      needs_guest = true;
      continue;
    }
    if (module->kind != QA_EXECUTION_MODULE_TYPESCRIPT) {
      const qa_resolved_resource_reference *artifact = (const qa_resolved_resource_reference *)module->artifact;
      const char *kind = module->kind == QA_EXECUTION_MODULE_QUAKEC ? "quakec" : module->kind == QA_EXECUTION_MODULE_QVM ? "qvm" : "native";
      qa_range_error("Application cannot execute %s %s module %s (%s): this executor is not joined to the shared simulation. Select a supported TypeScript execution module.",
          kind, module->role, module->owner.provider, artifact ? artifact->requested_path : "");
    }
  }
  qa_content_mounted *mounts = qa_open_mount_plan(&recipe.mounts, &mount_options);
  if (!mounts) { qa_app_loaded_content_destroy(loaded); return NULL; }
  loaded->native_mounts = mounts; loaded->mounts = mounts;
  if (pure) {
    launch_adapter adapter; memset(&adapter, 0, sizeof adapter); adapter.mounted = mounts;
    qa_launch_mounted launch_mounted; memset(&launch_mounted, 0, sizeof launch_mounted);
    launch_mounted.ctx = &adapter; launch_mounted.plan = qa_content_mounted_plan(mounts);
    launch_mounted.open = launch_adapter_open; launch_mounted.resolve = launch_adapter_resolve; launch_mounted.borrow_ordered = launch_adapter_borrow;
    qa_resource_request request = {recipe.map.geometry_content, recipe.map.geometry.requested_path};
    qa_resolved_resource_reference geometry;
    if (!qa_resolve_launch_resource(catalog, &launch_mounted, request, "map", &loaded->native_strings, &geometry)) {
      free(adapter.last_bytes); qa_app_loaded_content_destroy(loaded); return NULL;
    }
    free(adapter.last_bytes);
    loaded->owned_resources = calloc(recipe.resource_count ? recipe.resource_count : 1, sizeof *loaded->owned_resources);
    if (!loaded->owned_resources) { qa_app_loaded_content_destroy(loaded); return NULL; }
    for (size_t i = 0; i < recipe.resource_count; i++)
      loaded->owned_resources[i] = strcmp(recipe.resources[i].id, recipe.map.geometry.id) == 0 ? geometry : recipe.resources[i];
    recipe.mounts = *qa_content_mounted_plan(mounts);
    recipe.map.geometry = geometry;
    recipe.resources = loaded->owned_resources;
    loaded->recipe = recipe;
  }
  size_t byte_length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, &recipe.map.geometry, &byte_length);
  const char *map = recipe.map.geometry.requested_path;
  const char *bsp_family = qa_classify_bsp(bytes, byte_length, map);
  if (bsp_family == NULL) { free(bytes); qa_app_loaded_content_destroy(loaded); return NULL; }
  if (strcmp(bsp_family, "q1") == 0) {
    qa_content_mounted *map_content = open_map_content(catalog, &recipe, false);
    qa_content_opened_resource entities = {0}, lit = {0};
    char *ent_path = replace_bsp_suffix(map, ".ent"), *lit_path = replace_bsp_suffix(map, ".lit");
    bool has_ent = map_content && qa_content_mounted_open(map_content, ent_path, NULL, NULL, &entities);
    bool has_lit = map_content && qa_content_mounted_open(map_content, lit_path, NULL, NULL, &lit);
    sidecar_push(loaded, recipe.map.geometry_content, ent_path, has_ent ? entities.reference : NULL);
    sidecar_push(loaded, recipe.map.geometry_content, lit_path, has_lit ? lit.reference : NULL);
    qa_q1_map *world = calloc(1, sizeof *world);
    if (!world) qa_range_error("Out of memory");
    qa_q1_map_options map_options; memset(&map_options, 0, sizeof map_options);
    map_options.source = map;
    map_options.has_entities = has_ent; map_options.entities = entities.bytes; map_options.entities_length = entities.byte_length;
    map_options.has_lit = has_lit; map_options.lit = lit.bytes; map_options.lit_length = lit.byte_length;
    qa_q1_read_bsp(bytes, byte_length, &map_options, world);
    free(entities.bytes); free(lit.bytes);
    if (map_content) qa_content_mounted_destroy(map_content);
    loaded->world = world; loaded->world_family = 1; loaded->world_kind = "q1-bsp";
  } else if (strcmp(bsp_family, "q2") == 0) {
    qa_content_mounted *map_content = open_map_content(catalog, &recipe, false);
    qa_q2_bsp *raw = calloc(1, sizeof *raw);
    if (!raw) qa_range_error("Out of memory");
    qa_read_q2_bsp(bytes, byte_length, map, raw);
    material_stash stash; memset(&stash, 0, sizeof stash);
    stash.paths = calloc(raw->texture_info_count ? raw->texture_info_count : 1, sizeof *stash.paths);
    stash.bytes = calloc(raw->texture_info_count ? raw->texture_info_count : 1, sizeof *stash.bytes);
    stash.lengths = calloc(raw->texture_info_count ? raw->texture_info_count : 1, sizeof *stash.lengths);
    if (!stash.paths || !stash.bytes || !stash.lengths) qa_range_error("Out of memory");
    for (size_t i = 0; i < raw->texture_info_count; i++) {
      char *path = xformat("textures/%s.mat", raw->texture_info[i].name, NULL);
      if (!path) qa_range_error("Out of memory");
      bool duplicate = false;
      for (size_t j = 0; j < stash.count; j++) if (strcmp(stash.paths[j], path) == 0) duplicate = true;
      if (duplicate) { free(path); continue; }
      qa_content_opened_resource asset = {0};
      bool found = map_content && qa_content_mounted_open(map_content, path, NULL, NULL, &asset);
      char *sidecar_path = xstrdup(path);
      sidecar_push(loaded, recipe.map.geometry_content, sidecar_path, found ? asset.reference : NULL);
      stash.paths[stash.count] = path;
      stash.bytes[stash.count] = found ? asset.bytes : NULL;
      stash.lengths[stash.count] = found ? asset.byte_length : 0;
      stash.count++;
    }
    qa_q2_decoded_map *world = calloc(1, sizeof *world);
    if (!world) qa_range_error("Out of memory");
    qa_q2_map_resources resources = {&stash, stash_read_material};
    qa_to_q2_world_geometry(raw, &resources, world);
    free(raw);
    for (size_t i = 0; i < stash.count; i++) { free(stash.paths[i]); free(stash.bytes[i]); }
    free(stash.paths); free(stash.bytes); free(stash.lengths);
    if (map_content) qa_content_mounted_destroy(map_content);
    loaded->world = world; loaded->world_family = 2; loaded->world_kind = "q2-bsp";
  } else {
    qa_q3_decoded_world *world = calloc(1, sizeof *world);
    if (!world) qa_range_error("Out of memory");
    qa_q3_decode_world(bytes, byte_length, map, world);
    loaded->world = world; loaded->world_family = 3; loaded->world_kind = "q3-bsp";
  }
  free(bytes);
  bool has_quakec = false;
  for (size_t i = 0; i < recipe.execution_count; i++) if (recipe.execution[i].kind == QA_EXECUTION_MODULE_QUAKEC) has_quakec = true;
  if (has_quakec && network_is(&options, "native-server") && loaded->world_family != 1)
    qa_range_error("Native Quake clients require Quake BSP geometry");
  if (loaded->sidecar_count > 1) qsort(loaded->sidecars, loaded->sidecar_count, sizeof *loaded->sidecars, sidecar_compare);
  if (needs_guest && !unified) {
    /* prepareQuakeCSource/prepareQ3Game/prepareQ2Guest are host-shaped
     * in their slices; no concrete host exists to call yet. */
    qa_app_loaded_content_destroy(loaded);
    return NULL;
  }
  if (presentation_source == NULL) {
    if (recipe.weapon_behavior_count != 0) {
      bool guest_server = false;
      for (size_t i = 0; i < recipe.execution_count; i++)
        if (recipe.execution[i].role && strcmp(recipe.execution[i].role, "server-game") == 0 && recipe.execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT) guest_server = true;
      if (guest_server) qa_range_error("Selected native or bytecode game has no shared projectile behavior hook");
      for (size_t i = 0; i < recipe.weapon_behavior_count; i++)
        for (size_t j = i + 1; j < recipe.weapon_behavior_count; j++)
          if (recipe.weapon_behaviors[i].definition && recipe.weapon_behaviors[j].definition &&
              recipe.weapon_behaviors[i].definition->role && recipe.weapon_behaviors[j].definition->role &&
              strcmp(recipe.weapon_behaviors[i].definition->role, recipe.weapon_behaviors[j].definition->role) == 0)
            qa_range_error("Conflicting trajectory behaviors for one projectile role");
    }
    if (guest_host.prepare_weapon_behaviors) qa_app_loaded_content_prepare_weapon_behaviors(loaded);
    else if (recipe.weapon_behavior_count != 0) { qa_app_loaded_content_destroy(loaded); return NULL; }
    if (guest_host.prepare_mods) qa_app_loaded_content_prepare_mods(loaded, "gameplay");
    else if (recipe.mod_count != 0) { qa_app_loaded_content_destroy(loaded); return NULL; }
    if (guest_host.prepare_grapple) qa_app_loaded_content_prepare_qvm_grapple(loaded);
    else if (recipe.equipment.grapple.is_enabled && recipe.equipment.grapple.mechanic && strcmp(recipe.equipment.grapple.mechanic, "q3-qvm") == 0) { qa_app_loaded_content_destroy(loaded); return NULL; }
  } else if (unified) {
    if (guest_host.prepare_mods) qa_app_loaded_content_prepare_mods(loaded, "presentation");
    else if (recipe.mod_count != 0) { qa_app_loaded_content_destroy(loaded); return NULL; }
  }
  return loaded;
}
qa_app_options qa_app_application_options_for_recipe(const qa_app_options *options, const qa_content_catalog *catalog,
                                                     const qa_executable_recipe *recipe) {
  char prefix_family[4] = {0};
  const char *definition_provider = recipe->character.definition.provider;
  const char *colon = strchr(definition_provider, ':');
  size_t prefix_length = colon ? (size_t)(colon - definition_provider) : strlen(definition_provider);
  qa_game_family character;
  if (prefix_length == 2 && strncmp(definition_provider, "q1", 2) == 0) character = QA_GAME_FAMILY_Q1;
  else if (prefix_length == 2 && strncmp(definition_provider, "q2", 2) == 0) character = QA_GAME_FAMILY_Q2;
  else if (prefix_length == 2 && strncmp(definition_provider, "q3", 2) == 0) character = QA_GAME_FAMILY_Q3;
  else qa_range_error("Application input has no adapter for %s", definition_provider);
  snprintf(prefix_family, sizeof prefix_family, "%s", family_name(character));
  char model_prefix[16]; snprintf(model_prefix, sizeof model_prefix, "%s:model/", prefix_family);
  const char *appearance_provider = recipe->character.appearance.provider;
  if (strncmp(appearance_provider, model_prefix, strlen(model_prefix)) != 0)
    qa_range_error("Application character has no model selection for %s", appearance_provider);
  bool quakeworld_movement = false;
  for (size_t i = 0; i < recipe->timing_count; i++)
    if (strcmp(recipe->timing[i].provider, recipe->movement.provider) == 0)
      quakeworld_movement = recipe->timing[i].clock.kind == QA_CLOCK_Q1_QUAKEWORLD;
  const char *movement_provider = recipe->movement.provider;
  const char *movement_colon = strchr(movement_provider, ':');
  size_t movement_prefix = movement_colon ? (size_t)(movement_colon - movement_provider) : strlen(movement_provider);
  qa_game_family movement;
  if (movement_prefix == 2 && strncmp(movement_provider, "q1", 2) == 0) movement = QA_GAME_FAMILY_Q1;
  else if (movement_prefix == 2 && strncmp(movement_provider, "q2", 2) == 0) movement = QA_GAME_FAMILY_Q2;
  else if (movement_prefix == 2 && strncmp(movement_provider, "q3", 2) == 0) movement = QA_GAME_FAMILY_Q3;
  else qa_range_error("Application input has no adapter for %s", movement_provider);
  qa_app_options out = *options;
  out.product = xstrdup(qa_content_catalog_product(catalog, recipe->map.entities.content)->expectation.id);
  out.map = xstrdup(recipe->map.geometry.requested_path);
  out.map_product = xstrdup(qa_content_catalog_product(catalog, recipe->map.geometry_content)->expectation.id);
  out.movement = movement;
  out.movement_product = xstrdup(quakeworld_movement ? "q1-quakeworld"
      : qa_content_catalog_product(catalog, recipe->movement.content)->expectation.id);
  out.character = character;
  out.character_model = xstrdup(appearance_provider + strlen(model_prefix));
  const char *match_provider = recipe->match.provider;
  out.rules = strcmp(match_provider, "q1:ctf") == 0 || strcmp(match_provider, "q2:ctf") == 0 || strcmp(match_provider, "q3:ctf") == 0 ? "ctf"
      : strcmp(match_provider, "q2:lmctf") == 0 ? "lmctf"
      : strcmp(match_provider, "q2:tag") == 0 ? "tag"
      : strcmp(match_provider, "q2:deathball") == 0 ? "deathball"
      : strlen(match_provider) >= 6 && strcmp(match_provider + strlen(match_provider) - 6, ":horde") == 0 ? "horde" : "standard";
  if (!out.product || !out.map || !out.map_product || !out.movement_product || !out.character_model) qa_range_error("Out of memory");
  return out;
}
void qa_app_options_for_recipe_free(qa_app_options *options) {
  free(options->product); free(options->map); free(options->map_product);
  free(options->movement_product); free(options->character_model);
  options->product = options->map = options->map_product = options->movement_product = options->character_model = NULL;
}
