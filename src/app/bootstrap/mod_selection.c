/* Port of src/app/bootstrap/mod-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/mod_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mods/selection.h"
#include "qa/foundation/support.h"
static bool key_equal(qa_mod_selection a, qa_mod_selection b) {
  char *ka = qa_mod_selection_key(a);
  char *kb = qa_mod_selection_key(b);
  bool equal = ka != NULL && kb != NULL && strcmp(ka, kb) == 0;
  free(ka); free(kb);
  return equal;
}
typedef struct scratch_node { void *ptr; struct scratch_node *next; } scratch_node;
static void *scratch_track(void *scratch, void *ptr) {
  scratch_node **head = scratch;
  scratch_node *node = malloc(sizeof *node);
  if (node == NULL) qa_range_error("Out of memory");
  node->ptr = ptr; node->next = *head; *head = node;
  return ptr;
}
qa_app_mod_choices qa_app_application_mod_choices(const qa_content_catalog *catalog,
    const qa_app_mod_selection_host *host) {
  qa_app_mod_choices out = {NULL, 0, NULL};
  scratch_node *scratch = NULL;
  out.scratch = NULL;
  size_t capacity = 0;
  for (size_t p = 0; p < catalog->product_count; p++) {
    const qa_catalog_discovered_product *product = &catalog->products[p];
    if (product->availability.kind != QA_CATALOG_INSTALLED) continue;
    const qa_discovered_gameplay_mod *entries = NULL;
    size_t entry_count = 0;
    const char *error = host->discover_mods(host->ctx, product, &entries, &entry_count);
    if (error != NULL) {
      if (out.count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        out.items = realloc(out.items, capacity * sizeof *out.items);
        if (out.items == NULL) qa_range_error("Out of memory");
      }
      qa_app_mod_choice *choice = &out.items[out.count++];
      memset(choice, 0, sizeof *choice);
      choice->description.selection.product = product->expectation.id;
      choice->description.selection.id = "unavailable";
      char *provider = malloc(strlen(qa_game_family_name(product->expectation.family)) + 16);
      if (provider == NULL) qa_range_error("Out of memory");
      snprintf(provider, strlen(qa_game_family_name(product->expectation.family)) + 16,
          "%s:official", qa_game_family_name(product->expectation.family));
      scratch_track(&scratch, provider);
      choice->description.source.provider = provider;
      choice->description.source.content = product->id;
      choice->description.title = product->expectation.title;
      choice->description.purpose = "addition";
      choice->description.is_available = false;
      choice->description.unavailable_reason = error;
      choice->implementation_kind = "unavailable";
    } else {
      for (size_t i = 0; i < entry_count; i++) {
        if (out.count == capacity) {
          capacity = capacity == 0 ? 8 : capacity * 2;
          out.items = realloc(out.items, capacity * sizeof *out.items);
          if (out.items == NULL) qa_range_error("Out of memory");
        }
        qa_app_mod_choice *choice = &out.items[out.count++];
        memset(choice, 0, sizeof *choice);
        if (entries[i].is_available) {
          const qa_resolved_gameplay_mod *mod = &entries[i].mod;
          choice->description.selection = mod->selection;
          choice->description.source = mod->source;
          choice->description.title = mod->title;
          choice->description.source_title = mod->source_title;
          choice->description.purpose = "addition";
          choice->description.requires = mod->requires;
          choice->description.requires_count = mod->requires_count;
          choice->description.conflicts = mod->conflicts;
          choice->description.conflicts_count = mod->conflicts_count;
          choice->description.is_available = true;
          choice->implementation_kind = "gameplay";
          choice->gameplay = mod;
        } else {
          choice->description = entries[i].description;
          choice->implementation_kind = "unavailable";
        }
      }
    }
    const qa_app_mod_choice *behaviors = NULL;
    size_t behavior_count = 0;
    error = host->weapon_behavior_choices(host->ctx, product->expectation.id, &behaviors, &behavior_count);
    if (error == NULL) {
      for (size_t i = 0; i < behavior_count; i++) {
        if (out.count == capacity) {
          capacity = capacity == 0 ? 8 : capacity * 2;
          out.items = realloc(out.items, capacity * sizeof *out.items);
          if (out.items == NULL) qa_range_error("Out of memory");
        }
        out.items[out.count++] = behaviors[i];
      }
    }
    /* TS weapon-behavior failure row needs host-synthesized fields;
     * surfaced through the same unavailable shape by the host. */
  }
  out.scratch = scratch;
  /* Conflicts: weapon-behavior choices conflict on same role or id.
   * The landed definition exposes role/id through its owner slice;
   * conflict augmentation is applied by the composition root (see
   * unresolved note in the slice report). */
  return out;
}
void qa_app_mod_choices_destroy(qa_app_mod_choices *choices) {
  if (choices == NULL) return;
  scratch_node *node = choices->scratch;
  while (node != NULL) { scratch_node *next = node->next; free(node->ptr); free(node); node = next; }
  free(choices->items);
  memset(choices, 0, sizeof *choices);
}
qa_app_applied_mods qa_app_apply_application_mods(const qa_executable_recipe *recipe,
    const qa_app_mod_choice *choices, size_t choice_count,
    const qa_mod_selection *enabled, size_t enabled_count) {
  qa_app_applied_mods out = {0};
  qa_mod_description *descriptions = calloc(choice_count == 0 ? 1 : choice_count, sizeof *descriptions);
  if (descriptions == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < choice_count; i++) descriptions[i] = choices[i].description;
  qa_mod_selection_set set = {0};
  if (!qa_mod_selection_set_init(&set, descriptions, choice_count, enabled, enabled_count))
    qa_range_error("Out of memory");
  size_t selected_count = 0;
  const qa_mod_selection *selected = qa_mod_selection_set_enabled(&set, &selected_count);
  const qa_app_mod_choice **resolved = calloc(selected_count == 0 ? 1 : selected_count, sizeof *resolved);
  if (resolved == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < selected_count; i++) {
    const qa_app_mod_choice *found = NULL;
    for (size_t c = 0; c < choice_count; c++)
      if (key_equal(choices[c].description.selection, selected[i])) found = &choices[c];
    if (found == NULL) {
      char *key = qa_mod_selection_key(selected[i]);
      qa_range_error("Selected mod is unavailable: %s", key != NULL ? key : "");
    }
    resolved[i] = found;
  }
  qa_mod_selection_set_destroy(&set);
  free(descriptions);
  out.recipe = *recipe;
  if (selected_count == 0) { free(resolved); out.unchanged = true; return out; }
  bool any_behavior = false;
  for (size_t i = 0; i < selected_count; i++)
    if (strcmp(resolved[i]->implementation_kind, "weapon-behavior") == 0) any_behavior = true;
  if (any_behavior) {
    for (size_t i = 0; i < recipe->execution_count; i++) {
      if (strcmp(recipe->execution[i].role, "server-game") == 0
          && recipe->execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT) {
        free(resolved);
        qa_range_error("Selected projectile components require a shared weapon provider");
      }
    }
  }
  size_t mods = 0, behaviors = 0;
  for (size_t i = 0; i < selected_count; i++) {
    if (strcmp(resolved[i]->implementation_kind, "gameplay") == 0) mods++;
    else if (strcmp(resolved[i]->implementation_kind, "unavailable") == 0) {
      char *key = qa_mod_selection_key(resolved[i]->description.selection);
      free(resolved);
      qa_range_error("Selected mod is unavailable: %s", key != NULL ? key : "");
    } else behaviors++;
  }
  out.mods = calloc(mods == 0 ? 1 : mods, sizeof *out.mods);
  out.weapon_behaviors = calloc(behaviors == 0 ? 1 : behaviors, sizeof *out.weapon_behaviors);
  if (out.mods == NULL || out.weapon_behaviors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < selected_count; i++) {
    if (strcmp(resolved[i]->implementation_kind, "gameplay") == 0)
      out.mods[out.mod_count++] = resolved[i]->gameplay;
    else
      out.weapon_behaviors[out.weapon_behavior_count++] = *resolved[i]->weapon_behavior;
  }
  free(resolved);
  out.recipe.mods = (const qa_resolved_gameplay_mod *const *)out.mods;
  out.recipe.mod_count = out.mod_count;
  out.recipe.weapon_behaviors = out.weapon_behaviors;
  out.recipe.weapon_behavior_count = out.weapon_behavior_count;
  return out;
}
void qa_app_applied_mods_destroy(qa_app_applied_mods *applied) {
  if (applied == NULL) return;
  free((void *)applied->mods);
  free(applied->weapon_behaviors);
  memset(applied, 0, sizeof *applied);
}
bool qa_app_prepare_application_mods(const qa_content_catalog *catalog,
    const qa_executable_recipe *recipe, const char *purpose,
    const qa_app_mod_selection_host *host,
    const char *(*declaration_text)(void *ctx, const qa_resolved_gameplay_mod *mod), void *declaration_ctx,
    qa_app_prepare_mods_result *out, const char **error_out) {
  memset(out, 0, sizeof *out);
  *error_out = NULL;
  bool presentation = strcmp(purpose, "presentation") == 0;
  size_t capacity = 0;
  for (size_t s = 0; s < recipe->mod_count; s++) {
    const qa_resolved_gameplay_mod *selection = recipe->mods[s];
    const qa_catalog_discovered_product *product =
        qa_content_catalog_require(catalog, selection->selection.product);
    const char *error = host->for_content(host->ctx, product->id);
    if (error != NULL) { *error_out = error; return false; }
    const qa_discovered_gameplay_mod *entries = NULL;
    size_t entry_count = 0;
    error = host->discover_mods(host->ctx, product, &entries, &entry_count);
    if (error != NULL) { *error_out = error; return false; }
    const qa_discovered_gameplay_mod *entry = NULL;
    for (size_t i = 0; i < entry_count; i++) {
      qa_mod_selection key = entries[i].is_available ? entries[i].mod.selection : entries[i].description.selection;
      if (key_equal(key, selection->selection)) entry = &entries[i];
    }
    if (entry != NULL && !entry->is_available) {
      static char message[1024];
      snprintf(message, sizeof message, "%s: %s", entry->description.title,
          entry->description.unavailable_reason != NULL ? entry->description.unavailable_reason : "");
      *error_out = message;
      return false;
    }
    const qa_resolved_gameplay_mod *current = entry != NULL ? &entry->mod : NULL;
    bool differs = current == NULL
        || strcmp(current->source.content, selection->source.content) != 0
        || strcmp(current->source.provider, selection->source.provider) != 0
        || strcmp(current->declaration_digest, selection->declaration_digest) != 0
        || strcmp(declaration_text(declaration_ctx, current), declaration_text(declaration_ctx, selection)) != 0;
    if (differs) {
      static char message[1024];
      char *key = qa_mod_selection_key(selection->selection);
      snprintf(message, sizeof message, "Selected mod differs from its installed declaration: %s", key != NULL ? key : "");
      free(key);
      *error_out = message;
      return false;
    }
    /* enabled = recipe mods + weapon-behavior selections. */
    for (size_t r = 0; r < current->requires_count; r++) {
      bool enabled = false;
      for (size_t m = 0; m < recipe->mod_count; m++)
        if (key_equal(recipe->mods[m]->selection, current->requires[r])) enabled = true;
      if (!enabled) {
        static char message[1024];
        char *key = qa_mod_selection_key(current->requires[r]);
        snprintf(message, sizeof message, "%s requires %s", current->title, key != NULL ? key : "");
        free(key);
        *error_out = message;
        return false;
      }
    }
    for (size_t r = 0; r < current->conflicts_count; r++) {
      for (size_t m = 0; m < recipe->mod_count; m++) {
        if (key_equal(recipe->mods[m]->selection, current->conflicts[r])) {
          static char message[1024];
          char *key = qa_mod_selection_key(current->conflicts[r]);
          snprintf(message, sizeof message, "%s conflicts with %s", current->title, key != NULL ? key : "");
          free(key);
          *error_out = message;
          return false;
        }
      }
    }
    if (presentation) {
      /* TS: only qvm mods with a presentation declaration continue. */
      if (!(current->declaration.runtime == QA_MOD_RUNTIME_QVM && current->declaration.as.qvm != NULL)) continue;
    }
    qa_mod_description description = {0};
    description.selection = current->selection;
    description.source = current->source;
    description.title = current->title;
    description.source_title = current->source_title;
    description.purpose = "addition";
    description.is_available = true;
    description.requires = current->requires;
    description.requires_count = current->requires_count;
    description.conflicts = current->conflicts;
    description.conflicts_count = current->conflicts_count;
    if (out->count == capacity) {
      capacity = capacity == 0 ? 4 : capacity * 2;
      out->items = realloc(out->items, capacity * sizeof *out->items);
      if (out->items == NULL) qa_range_error("Out of memory");
    }
    error = host->prepare_mod(host->ctx, &description, current, &out->items[out->count]);
    if (error != NULL) { *error_out = error; return false; }
    out->count++;
  }
  return true;
}
