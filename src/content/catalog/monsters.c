/* Port of src/content/catalog/monsters.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/monsters.h"

#include "catalog_internal.h"
#include "qa/content/monsters/definitions.h"

qa_monster_definition_reference *qa_selected_monster_definitions(
    qa_enemy_selection enemies, size_t *out_count) {
  *out_count = 0;
  if (enemies.kind != QA_ENEMY_SELECTION_REPLACE) return NULL;
  size_t count = 0;
  if (!enemies.default_target.is_map_defined) count++;
  for (size_t i = 0; i < enemies.by_classname_count; i++)
    if (!enemies.by_classname[i].target.is_map_defined) count++;
  if (count == 0) return NULL;
  qa_monster_definition_reference *values =
      (qa_monster_definition_reference *)malloc(count * sizeof *values);
  if (values == NULL) return NULL;
  if (!enemies.default_target.is_map_defined)
    values[(*out_count)++] = enemies.default_target.reference;
  for (size_t i = 0; i < enemies.by_classname_count; i++)
    if (!enemies.by_classname[i].target.is_map_defined)
      values[(*out_count)++] = enemies.by_classname[i].target.reference;
  return values;
}

void qa_validate_monsters(qa_enemy_selection enemies,
                          const qa_content_catalog *catalog) {
  size_t count = 0;
  qa_monster_definition_reference *definitions =
      qa_selected_monster_definitions(enemies, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_monster_source_definition *source = qa_monster_source(definitions[i]);
    const qa_catalog_product_expectation *product =
        &qa_content_catalog_require(catalog, definitions[i].source.content)
             ->expectation;
    qa_game_family family;
    bool parsed = qa_game_family_parse(source->family, &family);
    if (!parsed || product->family != family ||
        strcmp(product->edition, source->edition) != 0 ||
        strcmp(product->campaign, source->program) != 0) {
      qa_range_error("Monster source content does not match %s: %s",
                     source->provider, definitions[i].source.content);
    }
  }
  free(definitions);
  if (enemies.kind == QA_ENEMY_SELECTION_REPLACE) {
    for (size_t i = 0; i < enemies.by_classname_count; i++)
      if (enemies.by_classname[i].classname == NULL ||
          enemies.by_classname[i].classname[0] == '\0')
        qa_range_error("An authored monster classname cannot be empty");
  }
}

qa_provider_timing *qa_selected_monster_timing(qa_enemy_selection enemies,
                                               size_t *out_count) {
  *out_count = 0;
  size_t count = 0;
  qa_monster_definition_reference *definitions =
      qa_selected_monster_definitions(enemies, &count);
  qa_provider_timing *values = NULL;
  size_t capacity = 0;
  for (size_t s = 0; s < qa_monster_source_count; s++) {
    bool wanted = false;
    for (size_t i = 0; i < count; i++)
      if (strcmp(definitions[i].source.provider,
                 qa_monster_sources[s].provider) == 0) wanted = true;
    if (!wanted) continue;
    if (*out_count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 4;
      qa_provider_timing *grown =
          (qa_provider_timing *)realloc(values, next * sizeof *grown);
      if (grown == NULL) { free(definitions); free(values); *out_count = 0; return NULL; }
      values = grown; capacity = next;
    }
    values[(*out_count)++] = qa_monster_timing(&qa_monster_sources[s]);
  }
  free(definitions);
  return values;
}

bool qa_monster_resources(qa_enemy_selection enemies,
                          qa_catalog_resource_list *out) {
  memset(out, 0, sizeof *out);
  size_t count = 0;
  qa_monster_definition_reference *definitions =
      qa_selected_monster_definitions(enemies, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_monster_source_definition *source = qa_monster_source(definitions[i]);
    const qa_monster_creature_entry *creature =
        qa_monster_source_creature(source, definitions[i].classname);
    if (creature == NULL) {
      qa_range_error("Missing registered monster resources: %s",
                     definitions[i].classname);
    }
    for (size_t r = 0; r < creature->resource_count; r++) {
      if (!qa_catalog_resource_push(out, definitions[i].source.content,
                                    creature->resources[r])) {
        free(definitions);
        qa_catalog_resource_list_free(out);
        return false;
      }
    }
  }
  free(definitions);
  return true;
}
