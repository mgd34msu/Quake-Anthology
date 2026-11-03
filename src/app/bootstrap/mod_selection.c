/* Port of src/app/bootstrap/mod-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/mod_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/native_mod.h"
#include "qa/app/bootstrap/simulation/quakec_mod.h"
#include "qa/app/bootstrap/simulation/qvm_mod.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/content/mods/selection.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/foundation/support.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *format(const char *a, const char *b, const char *c) {
  size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
  char *out = checked(malloc(n));
  snprintf(out, n, "%s%s%s", a, b, c);
  return out;
}
/* modSelectionKey(a) === modSelectionKey(b). */
static bool key_equal(qa_mod_selection a, qa_mod_selection b) {
  char *ka = qa_mod_selection_key(a);
  char *kb = qa_mod_selection_key(b);
  bool equal = ka != NULL && kb != NULL && strcmp(ka, kb) == 0;
  free(ka);
  free(kb);
  return equal;
}

/* --- C ownership of what the TS GC keeps alive for the rows --- */
typedef struct owned_list { void **items; size_t count, capacity; } owned_list;
static void *own(owned_list *list, void *p) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    list->items = checked(realloc(list->items, list->capacity * sizeof *list->items));
  }
  list->items[list->count++] = p;
  return p;
}
typedef struct discovered_set { qa_discovered_gameplay_mod *mods; size_t count; } discovered_set;
typedef struct mod_storage {
  owned_list blocks;      /* malloc'd strings, arrays and opened file bytes */
  owned_list discovered;  /* discovered_set * */
  owned_list behaviors;   /* qa_app_weapon_behavior_choice_list * */
  owned_list selections;  /* qa_mod_selection * read by qa_read_mod_selection */
  owned_list mounts;      /* qa_app_product_mounts * */
} mod_storage;
static void storage_free(mod_storage *s) {
  if (s == NULL) return;
  for (size_t i = 0; i < s->discovered.count; i++) {
    discovered_set *set = s->discovered.items[i];
    qa_discovered_gameplay_mods_free(set->mods, set->count);
    free(set);
  }
  for (size_t i = 0; i < s->behaviors.count; i++) {
    qa_app_weapon_behavior_choice_list_free(s->behaviors.items[i]);
    free(s->behaviors.items[i]);
  }
  for (size_t i = 0; i < s->selections.count; i++) {
    qa_mod_selection_free(s->selections.items[i]);
    free(s->selections.items[i]);
  }
  /* The discovered rows borrow the opened references; the mounts close last. */
  for (size_t i = 0; i < s->mounts.count; i++) qa_app_product_mounts_destroy(s->mounts.items[i]);
  for (size_t i = 0; i < s->blocks.count; i++) free(s->blocks.items[i]);
  free(s->discovered.items);
  free(s->behaviors.items);
  free(s->selections.items);
  free(s->mounts.items);
  free(s->blocks.items);
  free(s);
}

/* discoverGameplayMods(product, mounted) over a C mount set: the open callback
 * keeps every opened file's bytes in the storage. */
typedef struct discovery_mounts { qa_content_mounted *mounted; mod_storage *storage; } discovery_mounts;
static bool accept_content(void *ctx, const qa_content_mount *mount) {
  const char *content = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.content
                                                                : mount->as.loose.identity.content;
  return content != NULL && strcmp(content, ctx) == 0;
}
static bool discovery_open(void *ctx, const char *path, const char *content_filter, qa_mod_catalog_file *out) {
  discovery_mounts *d = ctx;
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(d->mounted, path, content_filter != NULL ? accept_content : NULL,
                               (void *)content_filter, &opened))
    return false;
  own(&d->storage->blocks, opened.bytes);
  out->bytes = opened.bytes;
  out->length = opened.byte_length;
  out->digest = opened.reference->digest;
  return true;
}
static discovered_set *discover(const qa_catalog_discovered_product *product, qa_content_mounted *mounted,
                                mod_storage *storage) {
  qa_mod_catalog_product adapter;
  memset(&adapter, 0, sizeof adapter);
  adapter.id = product->id;
  adapter.expectation.id = product->expectation.id;
  adapter.expectation.title = product->expectation.title;
  adapter.expectation.family = qa_game_family_name(product->expectation.family);
  adapter.expectation.edition = product->expectation.edition;
  discovery_mounts d = {mounted, storage};
  qa_mod_mounted_content content = {&d, discovery_open};
  discovered_set *set = own(&storage->discovered, checked(calloc(1, sizeof *set)));
  set->mods = qa_discover_gameplay_mods(&adapter, &content, &set->count);
  return set;
}

/* ---------------- applicationModChoices ---------------- */
typedef struct choice_list { qa_app_mod_choice *items; size_t count, capacity; } choice_list;
static qa_app_mod_choice *push_choice(choice_list *list) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    list->items = checked(realloc(list->items, list->capacity * sizeof *list->items));
  }
  qa_app_mod_choice *choice = &list->items[list->count++];
  memset(choice, 0, sizeof *choice);
  return choice;
}
static const char *source_title(const qa_catalog_discovered_product *product) {
  bool rerelease = product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0;
  switch (product->expectation.family) {
    case QA_GAME_FAMILY_Q1: return rerelease ? "Quake rerelease" : "Quake";
    case QA_GAME_FAMILY_Q2: return rerelease ? "Quake II rerelease" : "Quake II";
    case QA_GAME_FAMILY_Q3: break;
  }
  return rerelease ? "Quake III rerelease" : "Quake III";
}
typedef struct choices_step {
  qa_content_catalog *catalog;
  const qa_catalog_discovered_product *product;
  const char *source_title;
  choice_list *choices;
  mod_storage *storage;
} choices_step;
static void gameplay_choices(void *ctx) {
  choices_step *step = ctx;
  qa_app_product_mounts *mounts = qa_app_open_product_mounts(step->catalog, step->product->id, "gameplay-mods", false);
  own(&step->storage->mounts, mounts);
  discovered_set *set = discover(step->product, qa_app_product_mounts_mounted(mounts), step->storage);
  for (size_t i = 0; i < set->count; i++) {
    const qa_discovered_gameplay_mod *entry = &set->mods[i];
    qa_app_mod_choice *choice = push_choice(step->choices);
    if (entry->is_available) {
      const qa_resolved_gameplay_mod *mod = &entry->mod;
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
      choice->description = entry->description;
      choice->implementation_kind = "unavailable";
    }
  }
}
static void weapon_behavior_choices(void *ctx) {
  choices_step *step = ctx;
  qa_app_weapon_behavior_choice_list *list = own(&step->storage->behaviors, checked(calloc(1, sizeof *list)));
  *list = qa_app_weapon_behavior_choices_native(step->catalog, step->product->expectation.id);
  for (size_t i = 0; i < list->count; i++) {
    const qa_app_weapon_behavior_choice *entry = &list->items[i];
    if (entry->selection == NULL) continue;
    qa_mod_selection *selection = own(&step->storage->selections, checked(calloc(1, sizeof *selection)));
    if (!qa_read_mod_selection(entry->id, selection)) qa_range_error("Out of memory");
    qa_app_mod_choice *choice = push_choice(step->choices);
    choice->description.selection = *selection;
    choice->description.source = entry->selection->source;
    choice->description.title = entry->title;
    choice->description.source_title = step->source_title;
    choice->description.purpose = "addition";
    choice->description.is_available = entry->unavailable == NULL;
    choice->description.unavailable_reason = entry->unavailable;
    choice->implementation_kind = "weapon-behavior";
    choice->weapon_behavior = entry->selection;
  }
}
static void unavailable_row(choices_step *step, const char *id, const char *title, const char *reason) {
  qa_app_mod_choice *choice = push_choice(step->choices);
  choice->description.selection.product = step->product->expectation.id;
  choice->description.selection.id = id;
  choice->description.source.provider =
      own(&step->storage->blocks, format(qa_game_family_name(step->product->expectation.family), ":official", ""));
  choice->description.source.content = step->product->id;
  choice->description.title = title;
  choice->description.source_title = step->source_title;
  choice->description.purpose = "addition";
  choice->description.is_available = false;
  choice->description.unavailable_reason = own(&step->storage->blocks, format(reason, "", ""));
  choice->implementation_kind = "unavailable";
}
/* TS: two weapon-behavior choices conflict on the same definition role or id. */
static bool behaviors_conflict(const qa_app_mod_choice *a, const qa_app_mod_choice *b) {
  if (a == b || strcmp(a->implementation_kind, "weapon-behavior") != 0 || strcmp(b->implementation_kind, "weapon-behavior") != 0)
    return false;
  return strcmp(a->weapon_behavior->definition->role, b->weapon_behavior->definition->role) == 0 ||
         strcmp(a->weapon_behavior->definition->id, b->weapon_behavior->definition->id) == 0;
}
qa_app_mod_choices qa_app_application_mod_choices(qa_content_catalog *catalog) {
  mod_storage *storage = checked(calloc(1, sizeof *storage));
  choice_list choices = {NULL, 0, 0};
  for (size_t p = 0; p < catalog->product_count; p++) {
    const qa_catalog_discovered_product *product = &catalog->products[p];
    if (product->availability.kind != QA_CATALOG_INSTALLED) continue;
    choices_step step = {catalog, product, source_title(product), &choices, storage};
    qa_caught_error caught;
    if (!qa_try(gameplay_choices, &step, &caught)) unavailable_row(&step, "unavailable", product->expectation.title, caught.message);
    if (!qa_try(weapon_behavior_choices, &step, &caught)) {
      char *id = own(&storage->blocks, format("unavailable-weapon-behaviors", "", ""));
      for (;;) {
        bool taken = false;
        for (size_t i = 0; i < choices.count; i++)
          if (strcmp(choices.items[i].description.selection.product, product->expectation.id) == 0 &&
              strcmp(choices.items[i].description.selection.id, id) == 0)
            taken = true;
        if (!taken) break;
        id = own(&storage->blocks, format(id, "+", ""));
      }
      unavailable_row(&step, id, own(&storage->blocks, format(product->expectation.title, " weapon behaviors", "")), caught.message);
    }
  }
  /* Each trajectory adapter owns that launcher's path: description.conflicts gains every
   * other weapon-behavior choice on the same role or definition id. */
  for (size_t i = 0; i < choices.count; i++) {
    qa_app_mod_choice *choice = &choices.items[i];
    size_t extra = 0;
    for (size_t j = 0; j < choices.count; j++) extra += behaviors_conflict(&choices.items[j], choice);
    qa_mod_selection *conflicts = own(&storage->blocks, checked(calloc(choice->description.conflicts_count + extra + 1, sizeof *conflicts)));
    size_t at = 0;
    for (size_t j = 0; j < choice->description.conflicts_count; j++) conflicts[at++] = choice->description.conflicts[j];
    for (size_t j = 0; j < choices.count; j++)
      if (behaviors_conflict(&choices.items[j], choice)) conflicts[at++] = choices.items[j].description.selection;
    choice->description.conflicts = conflicts;
    choice->description.conflicts_count = at;
  }
  qa_app_mod_choices out = {choices.items, choices.count, storage};
  return out;
}
void qa_app_mod_choices_destroy(qa_app_mod_choices *choices) {
  if (choices == NULL) return;
  storage_free(choices->scratch);
  free(choices->items);
  memset(choices, 0, sizeof *choices);
}

/* ---------------- applyApplicationMods ---------------- */
static noreturn void unavailable_selection(qa_mod_selection selection) {
  char key[512];
  char *text = qa_mod_selection_key(selection);
  snprintf(key, sizeof key, "%s", text != NULL ? text : "");
  free(text);
  qa_raise("Error", 0, 0, "Selected mod is unavailable: %s", key);
}
qa_app_applied_mods qa_app_apply_application_mods(const qa_executable_recipe *recipe,
    const qa_app_mod_choice *choices, size_t choice_count,
    const qa_mod_selection *enabled, size_t enabled_count) {
  qa_app_applied_mods out;
  memset(&out, 0, sizeof out);
  qa_mod_description *descriptions = checked(calloc(choice_count + 1, sizeof *descriptions));
  qa_cleanup descriptions_cleanup;
  qa_cleanup_push(&descriptions_cleanup, free, descriptions);
  for (size_t i = 0; i < choice_count; i++) descriptions[i] = choices[i].description;
  qa_mod_selection_set set;
  memset(&set, 0, sizeof set);
  if (!qa_mod_selection_set_init(&set, descriptions, choice_count, enabled, enabled_count)) qa_range_error("Out of memory");
  size_t selected_count = 0;
  const qa_mod_selection *selected = qa_mod_selection_set_enabled(&set, &selected_count);
  const qa_app_mod_choice **resolved = checked(calloc(selected_count + 1, sizeof *resolved));
  qa_cleanup resolved_cleanup;
  qa_cleanup_push(&resolved_cleanup, free, resolved);
  for (size_t i = 0; i < selected_count; i++) {
    /* choices.find: the first choice with the key. */
    const qa_app_mod_choice *found = NULL;
    for (size_t c = 0; c < choice_count && found == NULL; c++)
      if (key_equal(choices[c].description.selection, selected[i])) found = &choices[c];
    if (found == NULL) unavailable_selection(selected[i]);
    resolved[i] = found;
  }
  qa_mod_selection_set_destroy(&set);
  out.recipe = *recipe;
  if (selected_count == 0) {
    qa_cleanup_pop(&resolved_cleanup);
    qa_cleanup_pop(&descriptions_cleanup);
    free(resolved);
    free(descriptions);
    out.unchanged = true;
    return out;
  }
  bool any_behavior = false;
  for (size_t i = 0; i < selected_count; i++)
    if (strcmp(resolved[i]->implementation_kind, "weapon-behavior") == 0) any_behavior = true;
  if (any_behavior)
    for (size_t i = 0; i < recipe->execution_count; i++)
      if (recipe->execution[i].role != NULL && strcmp(recipe->execution[i].role, "server-game") == 0 &&
          recipe->execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT)
        qa_raise("Error", 0, 0, "Selected projectile components require a shared weapon provider");
  size_t mods = 0, behaviors = 0;
  for (size_t i = 0; i < selected_count; i++)
    if (strcmp(resolved[i]->implementation_kind, "gameplay") == 0) mods++;
  /* weaponBehaviors' flatMap throws on the first unavailable selection. */
  for (size_t i = 0; i < selected_count; i++) {
    if (strcmp(resolved[i]->implementation_kind, "unavailable") == 0) unavailable_selection(resolved[i]->description.selection);
    if (strcmp(resolved[i]->implementation_kind, "weapon-behavior") == 0) behaviors++;
  }
  out.mods = checked(calloc(mods + 1, sizeof *out.mods));
  out.weapon_behaviors = checked(calloc(behaviors + 1, sizeof *out.weapon_behaviors));
  for (size_t i = 0; i < selected_count; i++) {
    if (strcmp(resolved[i]->implementation_kind, "gameplay") == 0) out.mods[out.mod_count++] = resolved[i]->gameplay;
    else out.weapon_behaviors[out.weapon_behavior_count++] = *resolved[i]->weapon_behavior;
  }
  qa_cleanup_pop(&resolved_cleanup);
  qa_cleanup_pop(&descriptions_cleanup);
  free(resolved);
  free(descriptions);
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

/* ---------------- prepareApplicationMods ---------------- */
typedef struct prepared_backing {
  mod_storage *storage;
  qa_mod_declaration_runtime *runtimes; /* per prepared mod, for its free */
  size_t capacity;
} prepared_backing;
static bool key_in(const char *const *keys, size_t count, qa_mod_selection selection) {
  char *key = qa_mod_selection_key(selection);
  bool found = false;
  for (size_t i = 0; i < count && !found; i++) found = key != NULL && strcmp(keys[i], key) == 0;
  free(key);
  return found;
}
static noreturn void raise_key(const char *prefix, const char *middle, qa_mod_selection selection) {
  char key[512];
  char *text = qa_mod_selection_key(selection);
  snprintf(key, sizeof key, "%s", text != NULL ? text : "");
  free(text);
  qa_raise("Error", 0, 0, "%s%s%s", prefix, middle, key);
}
static void prepared_free_one(qa_prepared_mod *mod, qa_mod_declaration_runtime runtime) {
  switch (runtime) {
    case QA_MOD_RUNTIME_QUAKEC: qa_app_sim_prepared_quakec_mod_free(mod); break;
    case QA_MOD_RUNTIME_QVM: qa_app_sim_prepared_qvm_mod_free(mod); break;
    case QA_MOD_RUNTIME_NATIVE: qa_app_sim_prepared_native_mod_free(mod); break;
  }
}
static void prepared_unwind(void *arg) {
  qa_app_prepared_mods_destroy(arg);
  free(arg);
}
qa_app_prepared_mods qa_app_prepare_application_mods(qa_content_catalog *catalog,
    const qa_executable_recipe *recipe, qa_app_mod_for_content_fn for_content, void *for_content_ctx,
    const char *purpose) {
  bool presentation = purpose != NULL && strcmp(purpose, "presentation") == 0;
  qa_app_prepared_mods *out = checked(calloc(1, sizeof *out));
  prepared_backing *backing = checked(calloc(1, sizeof *backing));
  backing->storage = checked(calloc(1, sizeof *backing->storage));
  out->backing = backing;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, prepared_unwind, out);
  size_t selection_count = recipe->mods != NULL ? recipe->mod_count : 0;
  size_t behavior_count = recipe->weapon_behaviors != NULL ? recipe->weapon_behavior_count : 0;
  /* dependencies: the recipe's mods; enabled adds the weapon-behavior selections, keyed by
   * { product: catalog.product(selection.source.content).expectation.id, id: definition.id }. */
  const char **dependencies = own(&backing->storage->blocks, checked(calloc(selection_count + 1, sizeof *dependencies)));
  const char **enabled = own(&backing->storage->blocks, checked(calloc(selection_count + behavior_count + 1, sizeof *enabled)));
  size_t enabled_count = 0;
  for (size_t i = 0; i < selection_count; i++) {
    dependencies[i] = own(&backing->storage->blocks, checked(qa_mod_selection_key(recipe->mods[i]->selection)));
    enabled[enabled_count++] = dependencies[i];
  }
  for (size_t i = 0; i < behavior_count; i++) {
    const qa_resolved_weapon_behavior_selection *behavior = &recipe->weapon_behaviors[i];
    qa_mod_selection key = {qa_content_catalog_product(catalog, behavior->source.content)->expectation.id,
                            behavior->definition->id};
    enabled[enabled_count++] = own(&backing->storage->blocks, checked(qa_mod_selection_key(key)));
  }
  for (size_t s = 0; s < selection_count; s++) {
    const qa_resolved_gameplay_mod *selection = recipe->mods[s];
    const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, selection->selection.product);
    qa_content_mounted *mounted = for_content(for_content_ctx, product->id);
    discovered_set *declarations = discover(product, mounted, backing->storage);
    const qa_discovered_gameplay_mod *entry = NULL;
    for (size_t i = 0; i < declarations->count && entry == NULL; i++) {
      qa_mod_selection key = declarations->mods[i].is_available ? declarations->mods[i].mod.selection
                                                                : declarations->mods[i].description.selection;
      if (key_equal(key, selection->selection)) entry = &declarations->mods[i];
    }
    if (entry != NULL && !entry->is_available)
      qa_raise("Error", 0, 0, "%s: %s", entry->description.title,
               entry->description.unavailable_reason != NULL ? entry->description.unavailable_reason : "");
    const qa_resolved_gameplay_mod *current = entry != NULL ? &entry->mod : NULL;
    bool differs = current == NULL || strcmp(current->source.content, selection->source.content) != 0 ||
                   strcmp(current->source.provider, selection->source.provider) != 0 ||
                   strcmp(current->declaration_digest, selection->declaration_digest) != 0;
    if (!differs) {
      /* JSON.stringify(current.declaration) !== JSON.stringify(selection.declaration) */
      char *left = qa_gameplay_mod_declaration_json(current->declaration);
      char *right = qa_gameplay_mod_declaration_json(selection->declaration);
      differs = strcmp(left, right) != 0;
      free(left);
      free(right);
    }
    if (differs) raise_key("Selected mod differs from its installed declaration: ", "", selection->selection);
    for (size_t r = 0; r < current->requires_count; r++)
      if (!key_in(enabled, enabled_count, current->requires[r])) raise_key(current->title, " requires ", current->requires[r]);
    for (size_t r = 0; r < current->conflicts_count; r++)
      if (key_in(enabled, enabled_count, current->conflicts[r])) raise_key(current->title, " conflicts with ", current->conflicts[r]);
    /* Projectile components initialize before the general operation registrations:
     * requires keeps only the recipe's own mods. */
    qa_mod_selection *requires = own(&backing->storage->blocks, checked(calloc(current->requires_count + 1, sizeof *requires)));
    size_t requires_count = 0;
    for (size_t r = 0; r < current->requires_count; r++)
      if (key_in(dependencies, selection_count, current->requires[r])) requires[requires_count++] = current->requires[r];
    qa_mod_description description;
    memset(&description, 0, sizeof description);
    description.selection = current->selection;
    description.source = current->source;
    description.title = current->title;
    description.source_title = current->source_title;
    description.purpose = "addition";
    description.is_available = true;
    description.requires = requires;
    description.requires_count = requires_count;
    description.conflicts = current->conflicts;
    description.conflicts_count = current->conflicts_count;
    if (presentation && (current->declaration.runtime != QA_MOD_RUNTIME_QVM || current->declaration.as.qvm->presentation == NULL))
      continue;
    if (out->count == backing->capacity) {
      backing->capacity = backing->capacity == 0 ? 4 : backing->capacity * 2;
      out->items = checked(realloc(out->items, backing->capacity * sizeof *out->items));
      backing->runtimes = checked(realloc(backing->runtimes, backing->capacity * sizeof *backing->runtimes));
    }
    qa_prepared_mod prepared;
    switch (current->declaration.runtime) {
      case QA_MOD_RUNTIME_QUAKEC:
        prepared = qa_app_sim_prepare_mounted_quakec_mod_content(description, current->declaration.as.quakec,
                                                                 current->declaration_digest, mounted);
        break;
      case QA_MOD_RUNTIME_QVM:
        prepared = qa_app_sim_prepare_mounted_qvm_mod(description, current->declaration.as.qvm, current->declaration_digest, mounted);
        break;
      case QA_MOD_RUNTIME_NATIVE:
      default:
        prepared = qa_app_sim_prepare_mounted_native_mod(description, current->declaration.as.native,
                                                         current->declaration_digest, mounted);
        break;
    }
    out->items[out->count] = prepared;
    backing->runtimes[out->count] = current->declaration.runtime;
    out->count++;
  }
  qa_cleanup_pop(&cleanup);
  qa_app_prepared_mods result = *out;
  free(out);
  return result;
}
void qa_app_prepared_mods_destroy(qa_app_prepared_mods *prepared) {
  if (prepared == NULL) return;
  prepared_backing *backing = prepared->backing;
  for (size_t i = 0; i < prepared->count; i++) prepared_free_one(&prepared->items[i], backing->runtimes[i]);
  free(prepared->items);
  if (backing != NULL) {
    storage_free(backing->storage);
    free(backing->runtimes);
    free(backing);
  }
  memset(prepared, 0, sizeof *prepared);
}
