/* Port of src/app/bootstrap/content.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  static char source_provider[32], match_provider[64];
  snprintf(source_provider, sizeof source_provider, "%s:official", family_text);
  out.source.provider = source_provider;
  out.source.content = product;
  out.rules = rules;
  bool guest_mod = family == QA_GAME_FAMILY_Q3 && !builtin;
  if (strcmp(rules, "standard") == 0 || guest_mod) out.match = out.source;
  else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q3) {
    out.match.provider = "q3:ctf"; out.match.content = product;
  } else if (strcmp(rules, "ctf") == 0 && family == QA_GAME_FAMILY_Q1) {
    char wanted[64]; snprintf(wanted, sizeof wanted, "q1-%s-ctf", rerelease ? "rerelease" : "classic");
    if (!host.require_product(host.ctx, wanted)) qa_range_error("Unknown content product %s", wanted);
    out.match.provider = "q1:ctf"; out.match.content = NULL; /* host resolves the id */
  } else if (strcmp(rules, "ctf") == 0 || strcmp(rules, "lmctf") == 0) {
    snprintf(match_provider, sizeof match_provider, "q2:%s", rules);
    out.match.provider = match_provider; out.match.content = NULL;
  } else if (strcmp(rules, "tag") == 0 || strcmp(rules, "deathball") == 0) {
    snprintf(match_provider, sizeof match_provider, "q2:%s", rules);
    out.match.provider = match_provider; out.match.content = NULL;
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
void qa_app_loaded_content_destroy(qa_app_loaded_content *content) {
  if (!content) return;
  qa_app_loaded_content_close(content);
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
const void *qa_app_loaded_content_for_content(qa_app_loaded_content *c, qa_content_id content_id) {
  const void *mounts = NULL;
  phase(c->host.for_content(c->host.ctx, c, content_id, &mounts));
  if (mounts && c->opened_count < 16) c->opened[c->opened_count++] = mounts;
  return mounts;
}
size_t qa_app_loaded_content_opened_mounts(qa_app_loaded_content *c, const void **out, size_t capacity) {
  if (c->closed) return 0;
  size_t count = 0;
  if (c->mounts && count < capacity) out[count++] = c->mounts;
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
