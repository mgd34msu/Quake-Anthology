/* Port of src/app/bootstrap/weapon-behavior-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/input/support.h"
static char *xdup_range(const char *s, size_t n) { char *r = qa_input_dup_range(s, n); if (!r) qa_range_error("Out of memory"); return r; }
qa_app_weapon_behavior_request qa_app_read_weapon_behavior_request(const char *value) {
  const char *slash = strchr(value, '/');
  size_t len = strlen(value);
  bool has_space = false;
  for (const char *p = value; *p; p++) if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') has_space = true;
  if (slash == NULL || slash == value || (size_t)(slash - value) == len - 1 || has_space || strchr(slash + 1, ':') == NULL)
    qa_range_error("Weapon behavior must be PRODUCT/DECLARED_ID");
  qa_app_weapon_behavior_request r = {xdup_range(value, (size_t)(slash - value)), NULL};
  r.id = qa_input_dup(slash + 1);
  if (!r.id) qa_range_error("Out of memory");
  return r;
}
void qa_app_weapon_behavior_request_free(qa_app_weapon_behavior_request *r) { free(r->product); free(r->id); memset(r, 0, sizeof *r); }
qa_app_options qa_app_configured_weapon_behavior_options(qa_app_options options, qa_cvar_registry *cvars) {
  if (options.has_weapon_behavior || options.mod_count != 0) return options;
  qa_cvar_read found; memset(&found, 0, sizeof found);
  const char *value = "";
  if (qa_cvar_registry_find(cvars, "qts_weaponBehavior", &found)) value = found.value;
  if (value[0] == '\0') {
    if (value != (const char *)"") qa_cvar_read_free(&found);
    return options;
  }
  qa_app_weapon_behavior_request request = qa_app_read_weapon_behavior_request(value);
  qa_cvar_read_free(&found);
  options.has_weapon_behavior = true;
  free(options.weapon_behavior_product); options.weapon_behavior_product = request.product;
  free(options.weapon_behavior_id); options.weapon_behavior_id = request.id;
  return options;
}
qa_app_weapon_behavior_choice *qa_app_weapon_behavior_choices(qa_app_weapon_behavior_host host, qa_content_catalog *catalog, const char *product_id, size_t *out_count) {
  return host.choices_for_product(host.ctx, catalog, product_id, out_count);
}
qa_executable_recipe qa_app_select_weapon_behavior(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                   qa_executable_recipe recipe, qa_app_weapon_behavior_request request,
                                                   qa_resolved_weapon_behavior_selection *selection_storage) {
  for (size_t i = 0; i < recipe.execution_count; i++) {
    const qa_resolved_execution_module *module = &recipe.execution[i];
    /* role "server-game" */
    if (strcmp(module->role, "server-game") == 0 && module->kind != QA_EXECUTION_MODULE_TYPESCRIPT)
      qa_range_error("Selected native or bytecode game has no shared projectile behavior hook; choose a TypeScript launcher provider");
  }
  size_t count = 0;
  qa_app_weapon_behavior_choice *choices = qa_app_weapon_behavior_choices(host, catalog, request.product, &count);
  const qa_resolved_weapon_behavior_selection *selected = NULL;
  const char *unavailable = NULL;
  for (size_t i = 0; i < count; i++) {
    if (choices[i].selection != NULL && strcmp(choices[i].selection->definition->id, request.id) == 0) selected = choices[i].selection;
    if (unavailable == NULL && choices[i].unavailable != NULL) unavailable = choices[i].unavailable;
  }
  if (selected == NULL) {
    char *reason = unavailable ? qa_input_dup(unavailable) : NULL;
    host.choices_free(host.ctx, choices, count);
    qa_range_error("Weapon behavior %s/%s: %s", request.product, request.id, reason ? reason : "No such declared compatible behavior");
  }
  *selection_storage = *selected;
  host.choices_free(host.ctx, choices, count);
  recipe.weapon_behaviors = selection_storage;
  recipe.weapon_behavior_count = 1;
  return recipe;
}
qa_app_prepared_configured_recipe qa_app_prepare_configured_recipe(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                                   const qa_app_options *options, qa_executable_recipe recipe) {
  size_t request_count = options->mod_count + (options->has_weapon_behavior ? 1 : 0);
  if (request_count == 0) { qa_app_prepared_configured_recipe r = {catalog, recipe}; return r; }
  qa_app_weapon_behavior_request *requests = calloc(request_count, sizeof *requests);
  if (!requests) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < options->mod_count; i++) {
    requests[at].product = qa_input_dup(options->mods[i].product);
    requests[at].id = qa_input_dup(options->mods[i].id);
    if (!requests[at].product || !requests[at].id) qa_range_error("Out of memory");
    at++;
  }
  if (options->has_weapon_behavior) {
    requests[at].product = qa_input_dup(options->weapon_behavior_product);
    requests[at].id = qa_input_dup(options->weapon_behavior_id);
    if (!requests[at].product || !requests[at].id) qa_range_error("Out of memory");
    at++;
  }
  bool missing = false;
  for (size_t i = 0; i < request_count; i++) if (!host.catalog_has_product(host.ctx, catalog, requests[i].product)) missing = true;
  if (missing) {
    qa_content_catalog *fresh = host.rediscover_with_mods(host.ctx, catalog);
    if (fresh != NULL) catalog = fresh;
  }
  qa_executable_recipe prepared = host.apply_mods(host.ctx, catalog, recipe, requests, request_count);
  for (size_t i = 0; i < request_count; i++) qa_app_weapon_behavior_request_free(&requests[i]);
  free(requests);
  qa_app_prepared_configured_recipe r = {catalog, prepared};
  return r;
}
qa_app_prepared_weapon_behavior qa_app_prepare_weapon_behavior(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                               qa_resolved_weapon_behavior_selection selection) {
  const void *mounts = host.for_content(host.ctx, selection.source.content);
  qa_app_prepared_weapon_behavior out; memset(&out, 0, sizeof out);
  const char *error = host.prepare(host.ctx, catalog, selection, mounts, &out);
  if (error != NULL) qa_range_error("%s", error);
  out.selection = selection;
  out.mounts = mounts;
  return out;
}
