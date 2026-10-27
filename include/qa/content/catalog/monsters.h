/*
 * Port of src/content/catalog/monsters.ts. Re-exports of
 * content/monsters (monsterSources, campaign slots, roster) live
 * in the qa/content/monsters headers and in the umbrella.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_MONSTERS_H
#define QA_CONTENT_CATALOG_MONSTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"

/* TS selectedMonsterDefinitions. Malloc'd array (free only). */
qa_monster_definition_reference *qa_selected_monster_definitions(
    qa_enemy_selection enemies, size_t *out_count);

/* TS validateMonsters: aborts with exact TS RangeErrors. */
void qa_validate_monsters(qa_enemy_selection enemies,
                          const qa_content_catalog *catalog);

/* TS selectedMonsterTiming. Malloc'd array (free only). */
qa_provider_timing *qa_selected_monster_timing(qa_enemy_selection enemies,
                                               size_t *out_count);

/* TS monsterResources. */
bool qa_monster_resources(qa_enemy_selection enemies,
                          qa_catalog_resource_list *out);

#endif
