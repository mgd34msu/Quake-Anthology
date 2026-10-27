/* Port of src/content/catalog/launch.ts. See launch.h for the
 * unported MountedContent / QVM-compatibility services.
 * GPL-2.0-or-later. */
#include "qa/content/catalog/launch.h"

#include "catalog_internal.h"
#include "qa/content/catalog/monsters.h"
#include "qa/content/catalog/timing.h"
#include "qa/content/monsters/definitions.h"

void qa_launch_string_list_free(qa_launch_string_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  memset(list, 0, sizeof *list);
}

static bool string_remember(qa_launch_string_list *list, char *owned) {
  if (owned == NULL) return false;
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    char **grown = (char **)realloc(list->values, next * sizeof *grown);
    if (grown == NULL) { free(owned); return false; }
    list->values = grown; list->capacity = next;
  }
  list->values[list->count++] = owned;
  return true;
}

static char *hex_of(qa_launch_string_list *strings, const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = (char *)malloc(length * 2 + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 15];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 15];
  }
  out[length * 2] = '\0';
  if (!string_remember(strings, out)) return NULL;
  return out;
}

qa_launch_choice qa_preset_choice(qa_recipe_id preset) {
  qa_launch_choice choice = {0};
  choice.preset = preset;
  return choice;
}

qa_selected_launch qa_select_launch(qa_launch_choice choice,
                                    const qa_launch_preset *preset,
                                    qa_recipe_id id) {
  if (strcmp(choice.preset, preset->id) != 0)
    qa_range_error("Requested preset %s does not match supplied preset %s",
                   choice.preset, preset->id);
  qa_selected_launch selected = {0};
  selected.base = *preset;
  selected.base.id = id != NULL ? id : preset->id;
  selected.preset = preset->id;
  if (choice.map.is_selected)
    selected.base.map = *(const qa_map_selection *)choice.map.value;
  if (choice.campaign.is_selected)
    selected.base.campaign = *(const qa_campaign_selection *)choice.campaign.value;
  if (choice.movement.is_selected)
    selected.base.movement = *(const qa_provider_reference *)choice.movement.value;
  if (choice.character.is_selected)
    selected.base.character =
        *(const qa_character_selection *)choice.character.value;
  if (choice.weapons.is_selected)
    selected.base.weapons =
        *(const qa_provider_reference_list *)choice.weapons.value;
  if (choice.enemies.is_selected)
    selected.base.enemies = *(const qa_enemy_selection *)choice.enemies.value;
  if (choice.equipment.is_selected)
    selected.base.equipment =
        *(const qa_equipment_selection *)choice.equipment.value;
  if (choice.presentation.is_selected)
    selected.base.presentation =
        *(const qa_presentation_selection *)choice.presentation.value;
  if (choice.engine_behavior.is_selected)
    selected.base.engine_behavior =
        *(const qa_provider_reference *)choice.engine_behavior.value;
  if (choice.combat.is_selected)
    selected.base.combat = *(const qa_provider_reference *)choice.combat.value;
  if (choice.inventory.is_selected)
    selected.base.inventory =
        *(const qa_provider_reference *)choice.inventory.value;
  if (choice.match.is_selected)
    selected.base.match = *(const qa_provider_reference *)choice.match.value;
  if (choice.transition.is_selected)
    selected.base.transition =
        *(const qa_provider_reference *)choice.transition.value;
  if (choice.execution.is_selected) {
    const qa_execution_selection_list *list =
        (const qa_execution_selection_list *)choice.execution.value;
    selected.base.execution = list->values;
    selected.base.execution_count = list->count;
  }
  return selected;
}

/* requiredContent. */
static bool content_seen(const qa_content_id *values, size_t count,
                         qa_content_id value) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(values[i], value) == 0) return true;
  return false;
}

static qa_content_id *required_content(const qa_selected_launch *launch,
                                       size_t *out_count) {
  qa_content_id *values = NULL;
  size_t count = 0, capacity = 0;
  struct refs { const qa_provider_reference *items; size_t count; } groups;
  (void)groups;
  qa_provider_reference fixed[32];
  size_t fixed_count = 0;
  fixed[fixed_count++] = launch->base.map.entities;
  fixed[fixed_count++] = launch->base.movement;
  fixed[fixed_count++] = launch->base.character.definition;
  fixed[fixed_count++] = launch->base.character.appearance;
  for (size_t i = 0; i < launch->base.weapons.count && fixed_count < 32; i++)
    fixed[fixed_count++] = launch->base.weapons.values[i];
  size_t equipment_count = 0;
  qa_provider_reference *equipment =
      qa_equipment_providers(launch->base.equipment, &equipment_count);
  qa_content_id *result = NULL;
  /* Build the full reference list dynamically instead. */
  qa_provider_reference *references = NULL;
  size_t reference_count = 0, reference_capacity = 0;
  #define PUSH_REF(ref)                                                       \
    do {                                                                      \
      if (reference_count == reference_capacity) {                            \
        size_t next = reference_capacity != 0 ? reference_capacity * 2 : 16;  \
        qa_provider_reference *grown = (qa_provider_reference *)realloc(      \
            references, next * sizeof *grown);                                \
        if (grown == NULL) { free(references); return NULL; }\
        references = grown; reference_capacity = next;                        \
      }                                                                       \
      references[reference_count++] = (ref);                                  \
    } while (0)
  for (size_t i = 0; i < fixed_count; i++) PUSH_REF(fixed[i]);
  for (size_t i = 0; i < equipment_count; i++) PUSH_REF(equipment[i]);
  PUSH_REF(launch->base.engine_behavior);
  PUSH_REF(launch->base.combat);
  PUSH_REF(launch->base.inventory);
  PUSH_REF(launch->base.match);
  PUSH_REF(launch->base.transition);
  PUSH_REF(launch->base.presentation.hud);
  PUSH_REF(launch->base.presentation.effects);
  PUSH_REF(launch->base.presentation.audio);
  for (size_t i = 0; i < launch->base.execution_count; i++)
    PUSH_REF(launch->base.execution[i].owner);
  if (launch->base.campaign.is_campaign) {
    PUSH_REF(launch->base.campaign.mission);
    PUSH_REF(launch->base.campaign.gamecode);
  }
  size_t monster_count = 0;
  qa_monster_definition_reference *monsters =
      qa_selected_monster_definitions(launch->base.enemies, &monster_count);
  for (size_t i = 0; i < monster_count; i++) PUSH_REF(monsters[i].source);
  free(monsters);
  if (launch->base.enemies.kind == QA_ENEMY_SELECTION_PLAYERS)
    PUSH_REF(launch->base.enemies.character);
  free(equipment);
  #undef PUSH_REF
  #define PUSH_CONTENT(value)                                                 \
    do {                                                                      \
      if (!content_seen(values, count, (value))) {                            \
        if (count == capacity) {                                              \
          size_t next = capacity != 0 ? capacity * 2 : 16;                    \
          qa_content_id *grown = (qa_content_id *)realloc(                    \
              values, next * sizeof *grown);                                  \
          if (grown == NULL) { free(values); free(references); return NULL; } \
          values = grown; capacity = next;                                    \
        }                                                                     \
        values[count++] = (value);                                            \
      }                                                                       \
    } while (0)
  PUSH_CONTENT(launch->base.map.geometry.content);
  PUSH_CONTENT(launch->base.presentation.assets);
  if (launch->base.presentation.environment.kind == QA_ENVIRONMENT_SELECTED)
    PUSH_CONTENT(launch->base.presentation.environment.resource.content);
  for (size_t i = 0; i < reference_count; i++) PUSH_CONTENT(references[i].content);
  for (size_t i = 0; i < launch->base.execution_count; i++) {
    const qa_execution_selection *module = &launch->base.execution[i];
    if (module->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
    PUSH_CONTENT(((const qa_resource_request *)module->artifact)->content);
  }
  #undef PUSH_CONTENT
  free(references);
  result = values;
  *out_count = count;
  return result;
}

static const char *mount_path_of(const qa_content_mount *mount) {
  return mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.archive_path
                                                 : mount->as.loose.root_path;
}
static const char *provenance_mount_path(const qa_resource_provenance *provenance) {
  return provenance->kind == QA_CONTENT_MOUNT_ARCHIVE
             ? provenance->archive_mount->archive_path
             : provenance->loose_mount->root_path;
}
static qa_mount_id mount_id_of(const qa_content_mount *mount) {
  return mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.id
                                                 : mount->as.loose.identity.id;
}

static bool order_for_content(qa_content_catalog *catalog,
                              const qa_resolved_mount_plan *plan,
                              qa_content_id content, qa_mount_id **out,
                              size_t *out_count) {
  *out = NULL; *out_count = 0;
  qa_catalog_mount_list first = {0};
  if (!qa_content_catalog_mounts_for(catalog, content, &first)) return false;
  size_t capacity = first.count + plan->default_order_count;
  qa_mount_id *order = capacity > 0
      ? (qa_mount_id *)malloc(capacity * sizeof *order) : NULL;
  size_t count = 0;
  for (size_t i = 0; i < first.count; i++) {
    const char *key = mount_path_of(&first.mounts[i]);
    for (size_t m = 0; m < plan->mount_count; m++) {
      if (strcmp(mount_path_of(&plan->mounts[m]), key) != 0) continue;
      qa_mount_id id = mount_id_of(&plan->mounts[m]);
      bool seen = false;
      for (size_t s = 0; s < count; s++) if (order[s] == id) seen = true;
      if (!seen && order != NULL) order[count++] = id;
    }
  }
  for (size_t i = 0; i < plan->default_order_count; i++) {
    bool seen = false;
    for (size_t s = 0; s < count; s++)
      if (order[s] == plan->default_order[i]) seen = true;
    if (!seen && order != NULL) order[count++] = plan->default_order[i];
  }
  qa_content_catalog_mount_list_free(&first);
  *out = order;
  *out_count = count;
  return true;
}

typedef struct accept_context {
  const char *kind;
  const qa_catalog_mount_list *allowed;
} accept_context;

static bool accept_mount(void *ctx, const qa_content_mount *mount) {
  accept_context *accept = (accept_context *)ctx;
  if (strcmp(accept->kind, "map") == 0) return true;
  for (size_t i = 0; i < accept->allowed->count; i++)
    if (strcmp(mount_path_of(&accept->allowed->mounts[i]),
               mount_path_of(mount)) == 0) return true;
  return false;
}

bool qa_resolve_launch_resource(qa_content_catalog *catalog,
                                qa_launch_mounted *mounted,
                                qa_resource_request request,
                                const char *kind,
                                qa_launch_string_list *strings,
                                qa_resolved_resource_reference *out) {
  qa_catalog_mount_list allowed = {0};
  if (!qa_content_catalog_mounts_for(catalog, request.content, &allowed))
    return false;
  qa_launch_mounted *reader = mounted;
  qa_launch_mounted borrowed = {0};
  qa_mount_id *order = NULL;
  size_t order_count = 0;
  if (strcmp(kind, "artifact") == 0) {
    if (!order_for_content(catalog, mounted->plan, request.content, &order,
                           &order_count)) {
      qa_content_catalog_mount_list_free(&allowed);
      return false;
    }
    char *hex = hex_of(strings, request.content);
    char *plan_id = hex != NULL ? qa_create_mount_plan_id("artifact", hex) : NULL;
    if (plan_id == NULL || !string_remember(strings, plan_id)) {
      free(plan_id); free(order);
      qa_content_catalog_mount_list_free(&allowed);
      return false;
    }
    if (!mounted->borrow_ordered(mounted->ctx, plan_id, order, order_count,
                                 &borrowed)) {
      free(order);
      qa_content_catalog_mount_list_free(&allowed);
      qa_catalog_error("Required resource is missing: %s/%s", request.content,
                       request.path);
    }
    reader = &borrowed;
  }
  accept_context accept = {kind, &allowed};
  qa_catalog_opened_resource opened = {0};
  bool found = reader->open(reader->ctx, request.path, accept_mount, &accept,
                            &opened);
  free(order);
  if (!found) {
    qa_content_catalog_mount_list_free(&allowed);
    qa_catalog_error("Required resource is missing: %s/%s", request.content,
                     request.path);
  }
  bool in_allowed = false;
  for (size_t i = 0; i < allowed.count; i++)
    if (strcmp(mount_path_of(&allowed.mounts[i]),
               provenance_mount_path(&opened.reference.provenance)) == 0)
      in_allowed = true;
  qa_content_catalog_mount_list_free(&allowed);
  if (!in_allowed)
    qa_catalog_error(
        "Required %s is absent from its selected content and base: %s/%s",
        kind, request.content, request.path);
  *out = opened.reference;
  return true;
}

void qa_validate_launch_roles(const qa_selected_launch *launch,
                              const qa_content_catalog *catalog) {
  struct role { const char *name; qa_provider_reference reference; };
  const struct role roles[] = {
      {"map entities", launch->base.map.entities},
      {"movement", launch->base.movement},
      {"character", launch->base.character.definition},
      {"engine behavior", launch->base.engine_behavior},
      {"combat", launch->base.combat},
      {"inventory", launch->base.inventory},
      {"match", launch->base.match},
      {"transition", launch->base.transition},
      {"hud", launch->base.presentation.hud},
      {"effects", launch->base.presentation.effects},
      {"audio", launch->base.presentation.audio}};
  for (size_t i = 0; i < sizeof roles / sizeof roles[0]; i++) {
    const char *provider = roles[i].reference.provider;
    const char *colon = strchr(provider, ':');
    size_t length = colon != NULL ? (size_t)(colon - provider) : strlen(provider);
    bool known = (length == 2 && strncmp(provider, "q1", 2) == 0) ||
                 (length == 2 && strncmp(provider, "q2", 2) == 0) ||
                 (length == 2 && strncmp(provider, "q3", 2) == 0);
    if (!known)
      qa_catalog_error("Launch %s provider %s has no known family", roles[i].name,
                       provider);
  }
  static const struct { const char *provider; const char *family; } bound[] = {
      {"q1:horde", "q1"}, {"q1:ctf", "q1"}, {"q2:ctf", "q2"},
      {"q2:horde", "q2"}, {"q3:horde", "q3"}, {"q3:ctf", "q3"}};
  const char *bound_family = NULL;
  for (size_t i = 0; i < sizeof bound / sizeof bound[0]; i++)
    if (strcmp(bound[i].provider, launch->base.match.provider) == 0)
      bound_family = bound[i].family;
  if (bound_family != NULL) {
    const char *world_family = qa_game_family_name(
        qa_content_catalog_product(catalog, launch->base.map.entities.content)
            ->expectation.family);
    if (strcmp(world_family, bound_family) != 0)
      qa_catalog_error(
          "Launch match provider %s requires a %s world, but map entities are %s",
          launch->base.match.provider, bound_family, world_family);
  }
}

void qa_prepare_launch_result_free(qa_prepare_launch_result *result) {
  free(result->weapons);
  for (size_t i = 0; i < result->owned_provider_count; i++)
    free(result->owned_providers[i]);
  free(result->owned_providers);
  qa_catalog_mount_plan_result_free(&result->plan);
  qa_launch_string_list_free(&result->strings);
  free(result->prefix_orders);
  for (size_t i = 0; i < result->prefix_mount_array_count; i++)
    free(result->prefix_mount_arrays[i]);
  free(result->prefix_mount_arrays);
  memset(result, 0, sizeof *result);
}

bool qa_prepare_launch_mount_plan(const qa_resolve_launch_options *options,
                                  qa_prepare_launch_result *out,
                                  qa_resolved_mount_plan *out_plan) {
  memset(out, 0, sizeof *out);
  qa_selected_launch choice = qa_select_launch(
      options->choice, options->preset, options->has_id ? options->id : NULL);
  out->selected = choice;
  if (choice.base.weapons.count > 0) {
    out->weapons = (qa_provider_reference *)malloc(
        choice.base.weapons.count * sizeof *out->weapons);
    if (out->weapons == NULL) return false;
    for (size_t i = 0; i < choice.base.weapons.count; i++) {
      char *owned = NULL;
      out->weapons[i] = qa_canonical_weapon_source(
          choice.base.map.entities, choice.base.weapons.values[i],
          options->catalog, &owned);
      if (owned != NULL) {
        char **grown = (char **)realloc(
            out->owned_providers,
            (out->owned_provider_count + 1) * sizeof *grown);
        if (grown == NULL) { free(owned); qa_prepare_launch_result_free(out); return false; }
        out->owned_providers = grown;
        out->owned_providers[out->owned_provider_count++] = owned;
      }
    }
    out->selected.base.weapons.values = out->weapons;
    out->selected.base.weapons.count = choice.base.weapons.count;
  }
  const qa_selected_launch *selected = &out->selected;
  qa_validate_equipment(selected->base.equipment, options->catalog);
  qa_validate_monsters(selected->base.enemies, options->catalog);
  qa_validate_launch_roles(selected, options->catalog);
  size_t required_count = 0;
  qa_content_id *required = required_content(selected, &required_count);
  if (required == NULL && required_count != 0) {
    qa_prepare_launch_result_free(out);
    return false;
  }
  for (size_t i = 0; i < required_count; i++)
    (void)qa_content_catalog_require(options->catalog, required[i]);
  for (size_t i = 0; i < selected->base.execution_count; i++) {
    for (size_t j = 0; j < i; j++) {
      if (strcmp(selected->base.execution[i].owner.provider,
                 selected->base.execution[j].owner.provider) == 0 &&
          strcmp(selected->base.execution[i].role,
                 selected->base.execution[j].role) == 0) {
        free(required);
        qa_catalog_error("Conflicting execution modules for %s/%s",
                         selected->base.execution[i].owner.provider,
                         selected->base.execution[i].role);
      }
    }
  }
  char *hex = hex_of(&out->strings, selected->base.id);
  char *plan_id = hex != NULL ? qa_create_mount_plan_id("launch", hex) : NULL;
  if (plan_id == NULL || !string_remember(&out->strings, plan_id)) {
    free(plan_id); free(required);
    qa_prepare_launch_result_free(out);
    return false;
  }
  qa_catalog_mount_plan_selection selection = {0};
  selection.id = plan_id;
  selection.assets = selected->base.presentation.assets;
  selection.geometry = selected->base.map.geometry.content;
  selection.rules = selected->base.combat.content;
  selection.explicit_presentation = options->choice.presentation.is_selected;
  selection.additional = (const qa_content_id *)required;
  selection.additional_count = required_count;
  bool ok = qa_content_catalog_create_mount_plan(options->catalog, &selection,
                                                 &out->plan);
  free(required);
  if (!ok) { qa_prepare_launch_result_free(out); return false; }
  /* Artifact prefix orders, prepended to the base plan's. */
  size_t base_prefixes = out->plan.plan.prefix_order_count;
  size_t artifact_prefixes = 0;
  for (size_t i = 0; i < selected->base.execution_count; i++)
    if (selected->base.execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT)
      artifact_prefixes++;
  *out_plan = out->plan.plan;
  if (artifact_prefixes > 0) {
    out->prefix_orders = (qa_prefix_mount_order *)calloc(
        artifact_prefixes + base_prefixes, sizeof *out->prefix_orders);
    out->prefix_mount_arrays = (qa_mount_id **)calloc(
        artifact_prefixes != 0 ? artifact_prefixes : 1,
        sizeof *out->prefix_mount_arrays);
    size_t slot = 0;
    size_t arrays = 0;
    for (size_t i = 0; i < selected->base.execution_count; i++) {
      const qa_execution_selection *module = &selected->base.execution[i];
      if (module->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
      const qa_resource_request *artifact =
          (const qa_resource_request *)module->artifact;
      char *normalized = qa_catalog_normalize_resource_path(artifact->path);
      if (normalized == NULL || !string_remember(&out->strings, normalized)) {
        free(normalized);
        continue;
      }
      /* TS groups by path and only emits single-source artifacts. */
      size_t sources = 0;
      for (size_t j = 0; j < selected->base.execution_count; j++) {
        const qa_execution_selection *other = &selected->base.execution[j];
        if (other->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
        const qa_resource_request *other_artifact =
            (const qa_resource_request *)other->artifact;
        char *other_normalized =
            qa_catalog_normalize_resource_path(other_artifact->path);
        bool same = other_normalized != NULL &&
                    strcmp(other_normalized, normalized) == 0;
        free(other_normalized);
        if (same) {
          bool duplicate = false;
          for (size_t k = 0; k < j; k++) {
            const qa_execution_selection *prior = &selected->base.execution[k];
            if (prior->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
            const qa_resource_request *prior_artifact =
                (const qa_resource_request *)prior->artifact;
            char *prior_normalized =
                qa_catalog_normalize_resource_path(prior_artifact->path);
            if (prior_normalized != NULL &&
                strcmp(prior_normalized, normalized) == 0 &&
                strcmp(prior_artifact->content, other_artifact->content) == 0)
              duplicate = true;
            free(prior_normalized);
          }
          if (!duplicate) sources++;
        }
      }
      bool first_for_path = true;
      for (size_t j = 0; j < i; j++) {
        const qa_execution_selection *other = &selected->base.execution[j];
        if (other->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
        const qa_resource_request *other_artifact =
            (const qa_resource_request *)other->artifact;
        char *other_normalized =
            qa_catalog_normalize_resource_path(other_artifact->path);
        if (other_normalized != NULL && strcmp(other_normalized, normalized) == 0)
          first_for_path = false;
        free(other_normalized);
      }
      if (sources != 1 || !first_for_path) continue;
      qa_mount_id *order = NULL;
      size_t order_count = 0;
      if (!order_for_content(options->catalog, &out->plan.plan,
                             artifact->content, &order, &order_count))
        continue;
      if (out->prefix_orders != NULL && out->prefix_mount_arrays != NULL) {
        out->prefix_mount_arrays[arrays++] = order;
        out->prefix_orders[slot].prefix = normalized;
        out->prefix_orders[slot].mounts = order;
        out->prefix_orders[slot].mount_count = order_count;
        slot++;
      } else {
        free(order);
      }
    }
    for (size_t i = 0; i < base_prefixes; i++)
      out->prefix_orders[slot++] = out->plan.plan.prefix_orders[i];
    out->prefix_mount_array_count = arrays;
    out_plan->prefix_orders = out->prefix_orders;
    out_plan->prefix_order_count = slot;
  }
  return true;
}

static bool resource_seen(const qa_resolved_resource_reference *values,
                          size_t count, qa_resource_id id) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(values[i].id, id) == 0) return true;
  return false;
}

static bool resources_push(qa_resolved_resource_reference **values, size_t *count,
                           size_t *capacity,
                           qa_resolved_resource_reference resource) {
  if (resource_seen(*values, *count, resource.id)) return true;
  if (*count == *capacity) {
    size_t next = *capacity != 0 ? *capacity * 2 : 8;
    qa_resolved_resource_reference *grown =
        (qa_resolved_resource_reference *)realloc(*values, next * sizeof *grown);
    if (grown == NULL) return false;
    *values = grown; *capacity = next;
  }
  (*values)[(*count)++] = resource;
  return true;
}

void qa_resolve_launch_result_free(qa_resolve_launch_result *result) {
  free(result->execution);
  free(result->artifacts);
  free(result->resources);
  for (size_t i = 0; i < result->timing.count; i++)
    free((void *)result->timing.values[i].numeric.id);
  qa_catalog_timing_list_free(&result->timing);
  free(result->ordering_providers);
  qa_launch_string_list_free(&result->strings);
  qa_selected_weapon_timing_free(result->weapon_timing, result->weapon_timing_count);
  qa_prepare_launch_result_free(&result->prepared);
  memset(result, 0, sizeof *result);
}

bool qa_resolve_launch(const qa_resolve_launch_options *options,
                       qa_resolve_launch_result *out) {
  memset(out, 0, sizeof *out);
  if (!qa_prepare_launch_mount_plan(options, &out->prepared, &out->plan))
    return false;
  const qa_selected_launch *selected = &out->prepared.selected;
  qa_launch_mounted mounted = {0};
  if (options->services.open_mount_plan == NULL ||
      !options->services.open_mount_plan(options->services.ctx, &out->plan,
                                         options->mounts_options, &mounted)) {
    qa_resolve_launch_result_free(out);
    qa_catalog_error("Required resource is missing: %s/%s",
                     selected->base.map.geometry.content,
                     selected->base.map.geometry.path);
  }
  qa_resolved_resource_reference *resources = NULL;
  size_t resource_count = 0, resource_capacity = 0;
  qa_resolved_resource_reference geometry = {0};
  if (!qa_resolve_launch_resource(options->catalog, &mounted,
                                  selected->base.map.geometry, "map",
                                  &out->strings, &geometry) ||
      !resources_push(&resources, &resource_count, &resource_capacity, geometry)) {
    free(resources);
    if (mounted.close != NULL) mounted.close(mounted.ctx);
    qa_resolve_launch_result_free(out);
    return false;
  }
  /* Source resource groups. */
  qa_catalog_resource_list groups[4] = {{0}};
  const char *group_kinds[4] = {"weapon", "equipment", "monster", "environment"};
  bool ok = qa_selected_weapon_resources(selected->base.map.entities,
                                         selected->base.weapons.values,
                                         selected->base.weapons.count,
                                         options->catalog, &groups[0]) &&
            qa_equipment_resources(selected->base.equipment, &groups[1]) &&
            qa_monster_resources(selected->base.enemies, &groups[2]);
  if (ok && selected->base.presentation.environment.kind == QA_ENVIRONMENT_SELECTED)
    ok = qa_catalog_resource_push(
        &groups[3],
        selected->base.presentation.environment.resource.content,
        selected->base.presentation.environment.resource.path);
  for (size_t g = 0; ok && g < 4; g++) {
    for (size_t r = 0; r < groups[g].count; r++) {
      qa_content_id content = groups[g].values[r].content;
      bool first = true;
      for (size_t prior = 0; prior < r; prior++)
        if (strcmp(groups[g].values[prior].content, content) == 0) first = false;
      if (!first) continue;
      qa_mount_id *order = NULL;
      size_t order_count = 0;
      ok = order_for_content(options->catalog, mounted.plan, content, &order,
                             &order_count);
      qa_catalog_mount_list allowed = {0};
      ok = ok && qa_content_catalog_mounts_for(options->catalog, content, &allowed);
      char *hex = ok ? hex_of(&out->strings, content) : NULL;
      char *reader_id = hex != NULL
          ? qa_create_mount_plan_id(group_kinds[g], hex) : NULL;
      if (reader_id == NULL || !string_remember(&out->strings, reader_id)) {
        free(reader_id); ok = false;
      }
      qa_launch_mounted source_mounts = {0};
      ok = ok && mounted.borrow_ordered(mounted.ctx, reader_id, order,
                                        order_count, &source_mounts);
      for (size_t q = 0; ok && q < groups[g].count; q++) {
        if (strcmp(groups[g].values[q].content, content) != 0) continue;
        qa_resolved_resource_reference resource = {0};
        bool found = source_mounts.resolve(source_mounts.ctx,
                                           groups[g].values[q].path, &resource);
        bool in_allowed = false;
        if (found)
          for (size_t a = 0; a < allowed.count; a++)
            if (strcmp(mount_path_of(&allowed.mounts[a]),
                       provenance_mount_path(&resource.provenance)) == 0)
              in_allowed = true;
        if (!found || !in_allowed) {
          free(order);
          qa_content_catalog_mount_list_free(&allowed);
          for (size_t gg = 0; gg < 4; gg++) qa_catalog_resource_list_free(&groups[gg]);
          free(resources);
          if (mounted.close != NULL) mounted.close(mounted.ctx);
          qa_resolve_launch_result_free(out);
          qa_catalog_error(
              "Required %s resource is absent from its selected content and base: %s/%s",
              group_kinds[g], content, groups[g].values[q].path);
        }
        ok = resources_push(&resources, &resource_count, &resource_capacity,
                            resource);
      }
      free(order);
      qa_content_catalog_mount_list_free(&allowed);
    }
  }
  for (size_t g = 0; g < 4; g++) qa_catalog_resource_list_free(&groups[g]);
  if (!ok) {
    free(resources);
    if (mounted.close != NULL) mounted.close(mounted.ctx);
    qa_resolve_launch_result_free(out);
    return false;
  }
  /* Execution modules. */
  size_t execution_count = selected->base.execution_count;
  out->execution = execution_count > 0
      ? (qa_execution_selection *)malloc(execution_count * sizeof *out->execution)
      : NULL;
  out->artifacts = execution_count > 0
      ? (qa_resolved_resource_reference *)calloc(execution_count,
                                                 sizeof *out->artifacts)
      : NULL;
  for (size_t i = 0; i < execution_count; i++) {
    const qa_execution_selection *module = &selected->base.execution[i];
    out->execution[i] = *module;
    if (module->kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
    const qa_resource_request *request =
        (const qa_resource_request *)module->artifact;
    qa_resolved_resource_reference artifact = {0};
    if (!qa_resolve_launch_resource(options->catalog, &mounted, *request,
                                    "artifact", &out->strings, &artifact) ||
        !resources_push(&resources, &resource_count, &resource_capacity, artifact)) {
      free(resources);
      if (mounted.close != NULL) mounted.close(mounted.ctx);
      qa_resolve_launch_result_free(out);
      return false;
    }
    out->artifacts[i] = artifact;
    out->execution[i].artifact = &out->artifacts[i];
    if (module->kind == QA_EXECUTION_MODULE_QVM) {
      qa_mount_id *order = NULL;
      size_t order_count = 0;
      (void)order_for_content(options->catalog, mounted.plan, request->content,
                              &order, &order_count);
      char *hex = hex_of(&out->strings, request->content);
      char *reader_id = hex != NULL ? qa_create_mount_plan_id("qvm-abi", hex) : NULL;
      if (reader_id != NULL) (void)string_remember(&out->strings, reader_id);
      qa_launch_mounted scoped = {0};
      const char *profile = NULL;
      if (reader_id != NULL &&
          mounted.borrow_ordered(mounted.ctx, reader_id, order, order_count,
                                 &scoped) &&
          options->services.read_qvm_compatibility != NULL) {
        const char *role =
            module->api.kind == QA_GAME_API_Q3 &&
                    module->api.as.q3.kind == QA_Q3_API_QAGAME
                ? "qagame"
                : module->api.kind == QA_GAME_API_Q3 &&
                          module->api.as.q3.kind == QA_Q3_API_CGAME
                      ? "cgame" : "ui";
        profile = options->services.read_qvm_compatibility(
            options->services.ctx, &scoped, artifact.requested_path,
            artifact.digest, role);
      }
      free(order);
      bool modern = profile != NULL && strcmp(profile, "q3-modern") == 0;
      qa_game_api_identity api = {0};
      api.kind = QA_GAME_API_Q3;
      if (strcmp(module->role, "server-game") == 0) {
        api.as.q3.kind = QA_Q3_API_QAGAME;
        api.as.q3.version = modern ? 8 : 7;
      } else if (strcmp(module->role, "client-game") == 0) {
        api.as.q3.kind = QA_Q3_API_CGAME;
        api.as.q3.version = modern ? 4 : 3;
      } else {
        api.as.q3.kind = QA_Q3_API_UI;
        api.as.q3.version = modern && module->api.kind == QA_GAME_API_Q3
                                 ? module->api.as.q3.version : 4;
      }
      out->execution[i].api = api;
    }
  }
  /* Timing and ordering. */
  qa_catalog_string_list selected_sources = {0};
  (void)qa_catalog_string_push(&selected_sources,
                               qa_catalog_dup(qa_equipment_provider_ids.threewave));
  (void)qa_catalog_string_push(&selected_sources,
                               qa_catalog_dup(qa_equipment_provider_ids.ctf));
  (void)qa_catalog_string_push(&selected_sources,
                               qa_catalog_dup(qa_equipment_provider_ids.lmctf));
  (void)qa_catalog_string_push(&selected_sources,
                               qa_catalog_dup(qa_equipment_provider_ids.hand_grenades));
  size_t equipment_provider_count = 0;
  qa_provider_reference *equipment_providers =
      qa_equipment_providers(selected->base.equipment, &equipment_provider_count);
  for (size_t i = 0; i < equipment_provider_count; i++)
    (void)qa_catalog_string_push(&selected_sources,
                                 qa_catalog_dup(equipment_providers[i].provider));
  free(equipment_providers);
  const char *weapon_providers[] = {
      qa_q1_weapon_providers.classic, qa_q1_weapon_providers.rerelease,
      qa_q1_hipnotic_weapon_providers.classic,
      qa_q1_hipnotic_weapon_providers.rerelease, qa_q2_weapon_providers.classic,
      qa_q2_weapon_providers.rerelease};
  for (size_t i = 0; i < 6; i++)
    (void)qa_catalog_string_push(&selected_sources, qa_catalog_dup(weapon_providers[i]));
  for (size_t i = 0; i < qa_monster_source_count; i++)
    (void)qa_catalog_string_push(&selected_sources,
                                 qa_catalog_dup(qa_monster_sources[i].provider));
  size_t monster_timing_count = 0;
  qa_provider_timing *monster_profiles =
      qa_selected_monster_timing(selected->base.enemies, &monster_timing_count);
  qa_provider_timing enemy_profile = {0};
  bool has_enemy_profile = false;
  if (selected->base.enemies.kind == QA_ENEMY_SELECTION_PLAYERS) {
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_require(options->catalog,
                                    selected->base.enemies.character.content)
             ->expectation;
    has_enemy_profile = qa_native_provider_timing(
        selected->base.enemies.character, product->family,
        strcmp(product->edition, "rerelease") == 0, &enemy_profile);
  }
  size_t weapon_timing_count = 0;
  qa_provider_timing *weapon_profiles = qa_selected_weapon_timing(
      selected->base.map.entities, selected->base.weapons.values,
      selected->base.weapons.count, options->catalog, &weapon_timing_count);
  out->weapon_timing = weapon_profiles;
  out->weapon_timing_count = weapon_timing_count;
  qa_catalog_timing_list timing = {0};
  for (size_t i = 0; i < selected->base.timing_count; i++) {
    if (qa_catalog_string_contains((const char *const *)selected_sources.values,
                                   selected_sources.count,
                                   selected->base.timing[i].provider)) continue;
    qa_provider_timing copy = selected->base.timing[i];
    copy.numeric.id = qa_catalog_dup(copy.numeric.id);
    (void)qa_catalog_timing_push(&timing, copy);
  }
  size_t equipment_timing_count = 0;
  qa_provider_timing *equipment_timing =
      qa_equipment_timing(selected->base.equipment, &equipment_timing_count);
  for (size_t i = 0; i < equipment_timing_count; i++) {
    qa_provider_timing copy = equipment_timing[i];
    copy.numeric.id = qa_catalog_dup(copy.numeric.id);
    (void)qa_catalog_timing_push(&timing, copy);
  }
  free(equipment_timing);
  for (size_t i = 0; i < monster_timing_count; i++) {
    qa_provider_timing copy = monster_profiles[i];
    copy.numeric.id = qa_catalog_dup(copy.numeric.id);
    (void)qa_catalog_timing_push(&timing, copy);
  }
  free(monster_profiles);
  if (has_enemy_profile) {
    bool in_timing = false;
    for (size_t i = 0; i < selected->base.timing_count; i++)
      if (strcmp(selected->base.timing[i].provider, enemy_profile.provider) == 0)
        in_timing = true;
    if (!in_timing) (void)qa_catalog_timing_push(&timing, enemy_profile);
    else free((void *)enemy_profile.numeric.id);
  }
  for (size_t i = 0; i < weapon_timing_count; i++)
    qa_admit_weapon_timing(&timing, weapon_profiles[i]);
  out->timing = timing;
  /* Ordering. */
  qa_catalog_string_list source_order = {0};
  size_t order_provider_count = 0;
  qa_provider_reference *order_providers =
      qa_equipment_providers(selected->base.equipment, &order_provider_count);
  for (size_t i = 0; i < order_provider_count; i++)
    (void)qa_catalog_string_push(&source_order,
                                 qa_catalog_dup(order_providers[i].provider));
  free(order_providers);
  {
    size_t count = 0;
    qa_provider_timing *profiles =
        qa_selected_monster_timing(selected->base.enemies, &count);
    for (size_t i = 0; i < count; i++)
      (void)qa_catalog_string_push(&source_order,
                                   qa_catalog_dup(profiles[i].provider));
    free(profiles);
  }
  if (has_enemy_profile)
    (void)qa_catalog_string_push(&source_order,
                                 qa_catalog_dup(enemy_profile.provider));
  qa_frame_ordering ordering = selected->base.ordering;
  qa_provider_id *ordering_providers = NULL;
  size_t ordering_count = 0;
  if (ordering.kind == QA_FRAME_ORDERING_MIXED) {
    qa_catalog_string_list existing = {0};
    for (size_t i = 0; i < ordering.provider_count; i++)
      if (!qa_catalog_string_contains((const char *const *)selected_sources.values,
                                      selected_sources.count, ordering.providers[i]))
        (void)qa_catalog_string_push(&existing, qa_catalog_dup(ordering.providers[i]));
    for (size_t i = 0; i < source_order.count; i++)
      (void)qa_catalog_string_push(&existing, qa_catalog_dup(source_order.values[i]));
    for (size_t i = 0; i < weapon_timing_count; i++)
      if (!qa_catalog_string_contains((const char *const *)existing.values,
                                      existing.count, weapon_profiles[i].provider))
        (void)qa_catalog_string_push(&existing,
                                     qa_catalog_dup(weapon_profiles[i].provider));
    ordering_providers = (qa_provider_id *)existing.values;
    ordering_count = existing.count;
    ordering.providers = (const qa_provider_id *)ordering_providers;
    ordering.provider_count = ordering_count;
  } else if (weapon_timing_count != 0 || has_enemy_profile) {
    qa_catalog_string_list providers = {0};
    (void)qa_catalog_string_push(&providers,
                                 qa_catalog_dup(selected->base.map.entities.provider));
    for (size_t i = 0; i < selected->base.timing_count; i++)
      if (!qa_catalog_string_contains((const char *const *)providers.values,
                                      providers.count,
                                      selected->base.timing[i].provider))
        (void)qa_catalog_string_push(&providers,
                                     qa_catalog_dup(selected->base.timing[i].provider));
    for (size_t i = 0; i < source_order.count; i++)
      if (!qa_catalog_string_contains((const char *const *)providers.values,
                                      providers.count, source_order.values[i]))
        (void)qa_catalog_string_push(&providers, qa_catalog_dup(source_order.values[i]));
    for (size_t i = 0; i < weapon_timing_count; i++)
      if (!qa_catalog_string_contains((const char *const *)providers.values,
                                      providers.count, weapon_profiles[i].provider))
        (void)qa_catalog_string_push(&providers,
                                     qa_catalog_dup(weapon_profiles[i].provider));
    ordering.kind = QA_FRAME_ORDERING_MIXED;
    ordering_providers = (qa_provider_id *)providers.values;
    ordering_count = providers.count;
    ordering.providers = (const qa_provider_id *)ordering_providers;
    ordering.provider_count = ordering_count;
  }
  out->ordering_providers = ordering_providers;
  qa_catalog_string_list_free(&source_order);
  qa_catalog_string_list_free(&selected_sources);
  /* Recipe. */
  qa_executable_recipe *recipe = &out->recipe;
  memset(recipe, 0, sizeof *recipe);
  recipe->weapon_behaviors = selected->base.weapon_behaviors;
  recipe->weapon_behavior_count = selected->base.weapon_behavior_count;
  recipe->mods = selected->base.mods;
  recipe->mod_count = selected->base.mod_count;
  recipe->schema_version = 3;
  recipe->id = selected->base.id;
  recipe->preset = selected->preset;
  recipe->map.geometry_content = selected->base.map.geometry.content;
  recipe->map.geometry = geometry;
  recipe->map.entities = selected->base.map.entities;
  recipe->campaign = selected->base.campaign;
  recipe->movement = selected->base.movement;
  recipe->character = selected->base.character;
  recipe->weapons = selected->base.weapons;
  recipe->equipment = selected->base.equipment;
  recipe->enemies = selected->base.enemies;
  recipe->presentation = selected->base.presentation;
  recipe->engine_behavior = selected->base.engine_behavior;
  recipe->combat = selected->base.combat;
  recipe->inventory = selected->base.inventory;
  recipe->match = selected->base.match;
  recipe->transition = selected->base.transition;
  recipe->execution = out->execution;
  recipe->execution_count = execution_count;
  recipe->mounts = out->plan;
  recipe->resources = resources;
  recipe->resource_count = resource_count;
  out->resources = resources;
  recipe->timing = timing.values;
  recipe->timing_count = timing.count;
  recipe->ordering = ordering;
  if (mounted.close != NULL) mounted.close(mounted.ctx);
  return true;
}

