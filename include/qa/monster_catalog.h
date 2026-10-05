#ifndef QA_MONSTER_CATALOG_H
#define QA_MONSTER_CATALOG_H

#include "qa/catalog.h"

typedef enum qa_monster_role {
    QA_MONSTER_TROOPER, QA_MONSTER_RANGED, QA_MONSTER_MELEE, QA_MONSTER_LEAPER,
    QA_MONSTER_GRENADIER, QA_MONSTER_ROCKET, QA_MONSTER_ARTILLERY, QA_MONSTER_HYBRID,
    QA_MONSTER_HEAVY, QA_MONSTER_FLYING, QA_MONSTER_AQUATIC, QA_MONSTER_BOSS,
    QA_MONSTER_SPECIAL
} qa_monster_role;
typedef struct qa_monster_catalog_creature {
    const char *classname;
    const char *const *resources;
    size_t resource_count;
} qa_monster_catalog_creature;
typedef struct qa_monster_catalog_source {
    const char *provider, *campaign;
    qa_game_family family;
    qa_product_edition edition;
    const qa_monster_catalog_creature *creatures;
    size_t creature_count;
} qa_monster_catalog_source;
typedef struct qa_monster_catalog_slot {
    const char *classname;
    qa_monster_role role;
} qa_monster_catalog_slot;

/* Authored content metadata only: source precache requirements and replacement
 * preferences, independent of GAME construction and live monster state. */
const qa_monster_catalog_source *qa_monster_catalog_sources(size_t *count);
const qa_monster_catalog_source *qa_monster_catalog_source_find(const char *provider);
const qa_monster_catalog_creature *qa_monster_catalog_creature_find(
    const qa_monster_catalog_source *, const char *classname);
const qa_monster_catalog_slot *qa_monster_catalog_slots(qa_game_family, size_t *count);
/* NULL preserves the authored monster. A role is a replacement preference,
 * never a claim that the two implementations have equivalent behavior. */
const qa_monster_catalog_creature *qa_monster_catalog_default(qa_game_family authored,
    const qa_monster_catalog_source *, const char *authored_classname);

#endif
