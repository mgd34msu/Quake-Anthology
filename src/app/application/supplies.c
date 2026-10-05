#include "supplies.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q1_supply.h"
#include "qa/game_q1_bots.h"
#include "guest_qc_internal.h"
#include "guest_native_q2_private.h"
#include "guest_q3_private.h"
#include "guest_q3_pickups.h"
#include "map_players_private.h"
#include "qa/application_supplies_save.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct supply_row {
    const char *source;
    const char *destinations[4];
} supply_row;

static const supply_row q1_q2_ammo[] = {
    {"q1:ammo/shells", {"q2:ammo_shells"}},
    {"q1:ammo/nails", {"q2:ammo_bullets"}},
    {"q1:ammo/rockets", {"q2:ammo_rockets", "q2:ammo_grenades"}},
    {"q1:ammo/cells", {"q2:ammo_cells"}}
};
static const supply_row q1_q2_weapons[] = {
    {"q1:weapon/axe", {"q2:weapon_blaster"}},
    {"q1:weapon/shotgun", {"q2:weapon_shotgun"}},
    {"q1:weapon/supershotgun", {"q2:weapon_supershotgun"}},
    {"q1:weapon/nailgun", {"q2:weapon_machinegun"}},
    {"q1:weapon/supernailgun", {"q2:weapon_chaingun"}},
    {"q1:weapon/grenadelauncher", {"q2:weapon_grenadelauncher"}},
    {"q1:weapon/rocketlauncher", {"q2:weapon_rocketlauncher"}},
    {"q1:weapon/lightning", {"q2:weapon_hyperblaster"}}
};
static const supply_row q1_q3_ammo[] = {
    {"q1:ammo/shells", {"q3:ammo/shotgun"}},
    {"q1:ammo/nails", {"q3:ammo/machinegun"}},
    {"q1:ammo/rockets", {"q3:ammo/rocketlauncher", "q3:ammo/grenadelauncher"}},
    {"q1:ammo/cells", {"q3:ammo/lightning"}}
};
static const supply_row q1_q3_weapons[] = {
    {"q1:weapon/axe", {"q3:weapon/gauntlet"}},
    {"q1:weapon/shotgun", {"q3:weapon/shotgun"}},
    {"q1:weapon/supershotgun", {"q3:weapon/shotgun"}},
    {"q1:weapon/nailgun", {"q3:weapon/machinegun"}},
    {"q1:weapon/supernailgun", {"q3:weapon/machinegun"}},
    {"q1:weapon/grenadelauncher", {"q3:weapon/grenadelauncher"}},
    {"q1:weapon/rocketlauncher", {"q3:weapon/rocketlauncher"}},
    {"q1:weapon/lightning", {"q3:weapon/lightning"}}
};
static const supply_row q2_q1_ammo[] = {
    {"q2:ammo_shells", {"q1:ammo/shells"}},
    {"q2:ammo_bullets", {"q1:ammo/nails"}},
    {"q2:ammo_cells", {"q2:ammo_cells", "q1:ammo/cells"}},
    {"q2:ammo_rockets", {"q1:ammo/rockets"}},
    {"q2:ammo_slugs", {"q1:ammo/nails"}},
    {"q2:ammo_grenades", {"q2:ammo_grenades", "q1:ammo/rockets"}}
};
static const supply_row q2_q1_weapons[] = {
    {"q2:weapon_blaster", {"q1:weapon/axe"}},
    {"q2:weapon_shotgun", {"q1:weapon/shotgun"}},
    {"q2:weapon_supershotgun", {"q1:weapon/supershotgun"}},
    {"q2:weapon_machinegun", {"q1:weapon/nailgun"}},
    {"q2:weapon_chaingun", {"q1:weapon/supernailgun"}},
    {"q2:ammo_grenades", {"q1:weapon/grenadelauncher"}},
    {"q2:weapon_grenadelauncher", {"q1:weapon/grenadelauncher"}},
    {"q2:weapon_rocketlauncher", {"q1:weapon/rocketlauncher"}},
    {"q2:weapon_hyperblaster", {"q1:weapon/lightning"}},
    {"q2:weapon_railgun", {"q1:weapon/supernailgun"}},
    {"q2:weapon_bfg", {"q1:weapon/lightning"}}
};
static const supply_row q2_q3_ammo[] = {
    {"q2:ammo_shells", {"q3:ammo/shotgun"}},
    {"q2:ammo_bullets", {"q3:ammo/machinegun"}},
    {"q2:ammo_cells", {"q2:ammo_cells", "q3:ammo/plasmagun", "q3:ammo/lightning", "q3:ammo/bfg"}},
    {"q2:ammo_rockets", {"q3:ammo/rocketlauncher"}},
    {"q2:ammo_slugs", {"q3:ammo/railgun"}},
    {"q2:ammo_grenades", {"q2:ammo_grenades", "q3:ammo/grenadelauncher"}}
};
static const supply_row q2_q3_weapons[] = {
    {"q2:weapon_shotgun", {"q3:weapon/shotgun"}},
    {"q2:weapon_supershotgun", {"q3:weapon/shotgun"}},
    {"q2:weapon_machinegun", {"q3:weapon/machinegun"}},
    {"q2:weapon_chaingun", {"q3:weapon/machinegun"}},
    {"q2:ammo_grenades", {"q3:weapon/grenadelauncher"}},
    {"q2:weapon_grenadelauncher", {"q3:weapon/grenadelauncher"}},
    {"q2:weapon_rocketlauncher", {"q3:weapon/rocketlauncher"}},
    {"q2:weapon_hyperblaster", {"q3:weapon/plasmagun", "q3:weapon/lightning"}},
    {"q2:weapon_railgun", {"q3:weapon/railgun"}},
    {"q2:weapon_bfg", {"q3:weapon/bfg"}}
};
static const supply_row q2_identity_ammo[] = {
    {"q2:ammo_shells", {NULL}}, {"q2:ammo_bullets", {NULL}},
    {"q2:ammo_grenades", {NULL}}, {"q2:ammo_rockets", {NULL}},
    {"q2:ammo_cells", {NULL}}, {"q2:ammo_slugs", {NULL}}
};
static const supply_row q3_q1_ammo[] = {
    {"q3:ammo/shotgun", {"q1:ammo/shells"}},
    {"q3:ammo/machinegun", {"q1:ammo/nails"}},
    {"q3:ammo/grenadelauncher", {"q1:ammo/rockets"}},
    {"q3:ammo/rocketlauncher", {"q1:ammo/rockets"}},
    {"q3:ammo/lightning", {"q1:ammo/cells"}},
    {"q3:ammo/railgun", {"q1:ammo/nails"}},
    {"q3:ammo/plasmagun", {"q1:ammo/cells"}},
    {"q3:ammo/bfg", {"q1:ammo/cells"}}
};
static const supply_row q3_q1_weapons[] = {
    {"q3:weapon/gauntlet", {"q1:weapon/axe"}},
    {"q3:weapon/shotgun", {"q1:weapon/supershotgun"}},
    {"q3:weapon/machinegun", {"q1:weapon/nailgun"}},
    {"q3:weapon/grenadelauncher", {"q1:weapon/grenadelauncher"}},
    {"q3:weapon/rocketlauncher", {"q1:weapon/rocketlauncher"}},
    {"q3:weapon/lightning", {"q1:weapon/lightning"}},
    {"q3:weapon/railgun", {"q1:weapon/supernailgun"}},
    {"q3:weapon/plasmagun", {"q1:weapon/lightning"}},
    {"q3:weapon/bfg", {"q1:weapon/lightning"}}
};
static const supply_row q3_q2_ammo[] = {
    {"q3:ammo/machinegun", {"q2:ammo_bullets"}},
    {"q3:ammo/shotgun", {"q2:ammo_shells"}},
    {"q3:ammo/grenadelauncher", {"q2:ammo_grenades"}},
    {"q3:ammo/rocketlauncher", {"q2:ammo_rockets"}},
    {"q3:ammo/lightning", {"q2:ammo_cells"}},
    {"q3:ammo/plasmagun", {"q2:ammo_cells"}},
    {"q3:ammo/bfg", {"q2:ammo_cells"}},
    {"q3:ammo/railgun", {"q2:ammo_slugs"}}
};
static const supply_row q3_q2_weapons[] = {
    {"q3:weapon/gauntlet", {"q2:weapon_blaster"}},
    {"q3:weapon/machinegun", {"q2:weapon_machinegun", "q2:weapon_chaingun"}},
    {"q3:weapon/shotgun", {"q2:weapon_shotgun", "q2:weapon_supershotgun"}},
    {"q3:weapon/grenadelauncher", {"q2:weapon_grenadelauncher"}},
    {"q3:weapon/rocketlauncher", {"q2:weapon_rocketlauncher"}},
    {"q3:weapon/lightning", {"q2:weapon_hyperblaster"}},
    {"q3:weapon/plasmagun", {"q2:weapon_hyperblaster"}},
    {"q3:weapon/railgun", {"q2:weapon_railgun"}},
    {"q3:weapon/bfg", {"q2:weapon_bfg"}}
};
static const supply_row q3_identity_ammo[] = {
    {"q3:ammo/machinegun", {NULL}}, {"q3:ammo/shotgun", {NULL}},
    {"q3:ammo/grenadelauncher", {NULL}}, {"q3:ammo/rocketlauncher", {NULL}},
    {"q3:ammo/lightning", {NULL}}, {"q3:ammo/railgun", {NULL}},
    {"q3:ammo/plasmagun", {NULL}}, {"q3:ammo/bfg", {NULL}}
};

typedef struct supply_alias { const char *item, *base; } supply_alias;
static const supply_alias ammo_aliases[] = {
    {"rogue:ammo/lava-nails", "q1:ammo/nails"},
    {"rogue:ammo/multi-rockets", "q1:ammo/rockets"},
    {"rogue:ammo/plasma", "q1:ammo/cells"},
    {"q2:ammo_magslug", "q2:ammo_slugs"},
    {"q2:ammo_flechettes", "q2:ammo_bullets"},
    {"q2:ammo_disruptor", "q2:ammo_cells"},
    {"q2:ammo_trap", "q2:ammo_grenades"},
    {"q2:ammo_tesla", "q2:ammo_grenades"},
    {"q2:ammo_prox", "q2:ammo_grenades"},
    {"q3:ammo/nailgun", "q3:ammo/machinegun"},
    {"q3:ammo/chaingun", "q3:ammo/machinegun"},
    {"q3:ammo/proxlauncher", "q3:ammo/grenadelauncher"}
};
static const supply_alias weapon_aliases[] = {
    {"q1:weapon/hipnotic:laser", "q1:weapon/lightning"},
    {"q1:weapon/hipnotic:mjolnir", "q1:weapon/axe"},
    {"q1:weapon/hipnotic:proximity", "q1:weapon/grenadelauncher"},
    {"q1:weapon/mg3:laser", "q1:weapon/lightning"},
    {"q1:weapon/mg3:mjolnir", "q1:weapon/axe"},
    {"q1:weapon/rogue:lava-nailgun", "q1:weapon/nailgun"},
    {"q1:weapon/rogue:lava-supernailgun", "q1:weapon/supernailgun"},
    {"q1:weapon/rogue:multi-grenade", "q1:weapon/grenadelauncher"},
    {"q1:weapon/rogue:multi-rocket", "q1:weapon/rocketlauncher"},
    {"q1:weapon/rogue:plasma", "q1:weapon/lightning"},
    {"q2:weapon_boomer", "q2:weapon_hyperblaster"},
    {"q2:weapon_phalanx", "q2:weapon_railgun"},
    {"q2:weapon_plasmabeam", "q2:weapon_hyperblaster"},
    {"q2:weapon_etf_rifle", "q2:weapon_machinegun"},
    {"q2:weapon_proxlauncher", "q2:weapon_grenadelauncher"},
    {"q2:weapon_disintegrator", "q2:weapon_bfg"},
    {"q2:weapon_chainfist", "q2:weapon_shotgun"},
    {"q2:ammo_trap", "q2:ammo_grenades"},
    {"q2:ammo_tesla", "q2:ammo_grenades"},
    {"q3:weapon/nailgun", "q3:weapon/machinegun"},
    {"q3:weapon/chaingun", "q3:weapon/machinegun"},
    {"q3:weapon/proxlauncher", "q3:weapon/grenadelauncher"}
};
static const supply_row rogue_ammo[] = {
    {"q1:ammo/nails", {"rogue:ammo/lava-nails"}},
    {"q1:ammo/rockets", {"rogue:ammo/multi-rockets"}},
    {"q1:ammo/cells", {"rogue:ammo/plasma"}}
};
static const supply_row rogue_weapons[] = {
    {"q1:weapon/nailgun", {"q1:weapon/rogue:lava-nailgun"}},
    {"q1:weapon/supernailgun", {"q1:weapon/rogue:lava-supernailgun"}},
    {"q1:weapon/grenadelauncher", {"q1:weapon/rogue:multi-grenade"}},
    {"q1:weapon/rocketlauncher", {"q1:weapon/rogue:multi-rocket"}},
    {"q1:weapon/lightning", {"q1:weapon/rogue:plasma"}}
};
static const supply_row mg3_weapons[] = {
    {"q1:weapon/lightning", {"q1:weapon/mg3:laser", "q1:weapon/mg3:mjolnir"}}
};
static const supply_row xatrix_ammo[] = {
    {"q2:ammo_grenades", {"q2:ammo_trap"}},
    {"q2:ammo_cells", {"q2:ammo_magslug"}},
    {"q2:ammo_slugs", {"q2:ammo_magslug"}}
};
static const supply_row xatrix_weapons[] = {
    {"q2:weapon_hyperblaster", {"q2:weapon_boomer", "q2:weapon_phalanx"}},
    {"q2:weapon_railgun", {"q2:weapon_phalanx"}}
};
static const supply_row q2_rogue_ammo[] = {
    {"q2:ammo_bullets", {"q2:ammo_flechettes"}},
    {"q2:ammo_grenades", {"q2:ammo_prox", "q2:ammo_tesla"}},
    {"q2:ammo_cells", {"q2:ammo_disruptor"}},
    {"q2:ammo_slugs", {"q2:ammo_disruptor"}}
};
static const supply_row q2_rogue_weapons[] = {
    {"q2:weapon_shotgun", {"q2:weapon_chainfist"}},
    {"q2:weapon_machinegun", {"q2:weapon_etf_rifle"}},
    {"q2:weapon_grenadelauncher", {"q2:weapon_proxlauncher"}},
    {"q2:weapon_hyperblaster", {"q2:weapon_plasmabeam", "q2:weapon_disintegrator"}},
    {"q2:weapon_railgun", {"q2:weapon_disintegrator"}}
};
static const supply_row team_ammo[] = {
    {"q3:ammo/machinegun", {"q3:ammo/nailgun", "q3:ammo/chaingun"}},
    {"q3:ammo/grenadelauncher", {"q3:ammo/proxlauncher"}}
};
static const supply_row team_weapons[] = {
    {"q3:weapon/machinegun", {"q3:weapon/nailgun", "q3:weapon/chaingun"}},
    {"q3:weapon/grenadelauncher", {"q3:weapon/proxlauncher"}}
};
static const supply_row q1_hipnotic_weapons[] = {
    {"q1:weapon/lightning", {"q1:weapon/hipnotic:laser", "q1:weapon/hipnotic:mjolnir"}},
    {"q1:weapon/grenadelauncher", {"q1:weapon/hipnotic:proximity"}}
};
static const supply_row q2_hipnotic_weapons[] = {
    {"q2:weapon_hyperblaster", {"q1:weapon/hipnotic:laser"}},
    {"q2:weapon_grenadelauncher", {"q1:weapon/hipnotic:proximity"}},
    {"q2:weapon_bfg", {"q1:weapon/hipnotic:mjolnir"}}
};
static const supply_row q3_hipnotic_weapons[] = {
    {"q3:weapon/plasmagun", {"q1:weapon/hipnotic:laser"}},
    {"q3:weapon/grenadelauncher", {"q1:weapon/hipnotic:proximity"}},
    {"q3:weapon/bfg", {"q1:weapon/hipnotic:mjolnir"}}
};
static const supply_alias q2_q1_owners[] = {
    {"q1:weapon/axe", "q2:weapon_blaster"},
    {"q1:weapon/shotgun", "q2:weapon_shotgun"},
    {"q1:weapon/supershotgun", "q2:weapon_supershotgun"},
    {"q1:weapon/nailgun", "q2:weapon_machinegun"},
    {"q1:weapon/supernailgun", "q2:weapon_chaingun"},
    {"q1:weapon/grenadelauncher", "q2:weapon_grenadelauncher"},
    {"q1:weapon/rocketlauncher", "q2:weapon_rocketlauncher"},
    {"q1:weapon/lightning", "q2:weapon_hyperblaster"}
};
static const supply_alias q2_q3_owners[] = {
    {"q3:weapon/shotgun", "q2:weapon_shotgun"},
    {"q3:weapon/machinegun", "q2:weapon_machinegun"},
    {"q3:weapon/grenadelauncher", "q2:weapon_grenadelauncher"},
    {"q3:weapon/rocketlauncher", "q2:weapon_rocketlauncher"},
    {"q3:weapon/plasmagun", "q2:weapon_hyperblaster"},
    {"q3:weapon/lightning", "q2:weapon_hyperblaster"},
    {"q3:weapon/railgun", "q2:weapon_railgun"},
    {"q3:weapon/bfg", "q2:weapon_bfg"}
};
static const supply_alias q3_q1_owners[] = {
    {"q1:ammo/shells", "q3:ammo/shotgun"},
    {"q1:ammo/nails", "q3:ammo/machinegun"},
    {"q1:ammo/rockets", "q3:ammo/rocketlauncher"},
    {"q1:ammo/cells", "q3:ammo/lightning"}
};
static const supply_alias q3_q2_owners[] = {
    {"q2:ammo_bullets", "q3:ammo/machinegun"},
    {"q2:ammo_shells", "q3:ammo/shotgun"},
    {"q2:ammo_grenades", "q3:ammo/grenadelauncher"},
    {"q2:ammo_rockets", "q3:ammo/rocketlauncher"},
    {"q2:ammo_cells", "q3:ammo/lightning"},
    {"q2:ammo_slugs", "q3:ammo/railgun"}
};
static const supply_alias rogue_periodic[] = {
    {"rogue:ammo/lava-nails", "q3:ammo/nailgun"},
    {"rogue:ammo/multi-rockets", "q3:ammo/proxlauncher"},
    {"rogue:ammo/plasma", "q3:ammo/plasmagun"}
};
static const supply_alias xatrix_periodic[] = {
    {"q2:ammo_magslug", "q3:ammo/railgun"},
    {"q2:ammo_trap", "q3:ammo/proxlauncher"}
};
static const supply_alias q2_rogue_periodic[] = {
    {"q2:ammo_flechettes", "q3:ammo/nailgun"},
    {"q2:ammo_prox", "q3:ammo/proxlauncher"},
    {"q2:ammo_tesla", "q3:ammo/proxlauncher"},
    {"q2:ammo_disruptor", "q3:ammo/bfg"}
};
static const supply_alias team_periodic[] = {
    {"q3:ammo/nailgun", "q3:ammo/machinegun"},
    {"q3:ammo/chaingun", "q3:ammo/machinegun"},
    {"q3:ammo/proxlauncher", "q3:ammo/grenadelauncher"}
};

typedef struct supply_pair {
    struct supply_pair *next;
    application_supplies *owner;
    application_provider *source, *arsenal;
    qa_string_id profile_id;
    qa_supply_profile profile;
    qa_supply *supply;
    qa_pickup_rule *pickup_rules;
    size_t pickup_rule_count;
    bool native;
} supply_pair;
typedef struct supply_timer {
    qa_item_id item, source;
    int32_t elapsed;
    qa_q3_weapon weapon;
} supply_timer;
typedef struct supply_actor {
    struct supply_actor *next;
    supply_pair *pair;
    qa_actor_id actor;
    supply_timer *timers;
    size_t timer_count;
    bool timers_ready;
    qa_pickup_lease pickups;
    bool pickup_imported;
} supply_actor;
struct application_supplies {
    qa_application *application;
    qa_inventory *inventory;
    supply_pair *pairs;
    supply_actor *actors;
    size_t calls;
    bool admitting;
};

static bool intern(application_supplies *owner, const char *text, qa_item_id *out,
                    qa_error *error) {
    return qa_strings_intern_cstr(qa_session_strings(owner->application->session), text, out, error);
}
static qa_supply_mapping *mapping(qa_supply_mapping *rows, size_t count, qa_item_id source) {
    for (size_t i = 0; i < count; ++i) if (rows[i].source == source) return rows + i;
    return NULL;
}
static bool append_mapping(qa_supply_mapping **rows, size_t *count, qa_item_id source,
    const qa_item_id *items, size_t item_count, qa_error *error) {
    if (*count == SIZE_MAX / sizeof(**rows) || item_count > SIZE_MAX / sizeof(*items))
        return application_fail(error, QA_ERROR_MEMORY, "Supply profile exceeds native extent");
    qa_item_id *destinations = malloc(item_count * sizeof(*destinations));
    if (!destinations) return application_fail(error, QA_ERROR_MEMORY, "Allocating supply destinations");
    qa_supply_mapping *grown = realloc(*rows, (*count + 1) * sizeof(*grown));
    if (!grown) { free(destinations); return application_fail(error, QA_ERROR_MEMORY, "Allocating supply mappings"); }
    memcpy(destinations, items, item_count * sizeof(*destinations));
    *rows = grown; grown[(*count)++] = (qa_supply_mapping){source, destinations, item_count};
    return true;
}
static bool install_rows(application_supplies *owner, qa_supply_mapping **rows, size_t *count,
    const supply_row *source, size_t source_count, bool identity, qa_error *error) {
    for (size_t i = 0; i < source_count; ++i) {
        qa_item_id id, items[4]; size_t used = 0;
        if (!intern(owner, source[i].source, &id, error)) return false;
        if (identity) items[used++] = id;
        else for (size_t j = 0; j < 4 && source[i].destinations[j]; ++j) {
            if (!intern(owner, source[i].destinations[j], items + used, error)) return false;
            ++used;
        }
        if (!append_mapping(rows, count, id, items, used, error)) return false;
    }
    return true;
}
static bool add_destination(qa_supply_mapping *row, qa_item_id item, qa_error *error) {
    for (size_t i = 0; i < row->count; ++i) if (row->destinations[i] == item) return true;
    if (row->count == SIZE_MAX / sizeof(item))
        return application_fail(error, QA_ERROR_MEMORY, "Supply destinations exceed native extent");
    qa_item_id *items = realloc((void *)row->destinations, (row->count + 1) * sizeof(*items));
    if (!items) return application_fail(error, QA_ERROR_MEMORY, "Extending supply destinations");
    row->destinations = items; items[row->count++] = item; return true;
}
static bool expand_rows(application_supplies *owner, qa_supply_mapping *rows, size_t count,
    const supply_row *extensions, size_t extension_count, qa_error *error) {
    for (size_t i = 0; i < count; ++i) {
        size_t original_count = rows[i].count;
        for (size_t j = 0; j < original_count; ++j)
            for (size_t k = 0; k < extension_count; ++k) {
                qa_item_id source;
                if (!intern(owner, extensions[k].source, &source, error)) return false;
                if (rows[i].destinations[j] != source) continue;
                for (size_t n = 0; n < 4 && extensions[k].destinations[n]; ++n) {
                    qa_item_id item;
                    if (!intern(owner, extensions[k].destinations[n], &item, error) ||
                        !add_destination(rows + i, item, error)) return false;
                }
            }
    }
    return true;
}
static bool alias_rows(application_supplies *owner, qa_supply_mapping **rows, size_t *count,
    const supply_alias *aliases, size_t alias_count, qa_error *error) {
    size_t original_count = *count;
    for (size_t i = 0; i < alias_count; ++i) {
        qa_item_id item, base;
        if (!intern(owner, aliases[i].item, &item, error) || !intern(owner, aliases[i].base, &base, error)) return false;
        qa_supply_mapping *original = mapping(*rows, original_count, base);
        if (!original || mapping(*rows, original_count, item)) continue;
        if (!append_mapping(rows, count, item, original->destinations, original->count, error)) return false;
    }
    return true;
}

static void mappings_free(qa_supply_mapping *rows, size_t count) {
    for (size_t i = 0; i < count; ++i) free((void *)rows[i].destinations);
    free(rows);
}
static void profile_free(qa_supply_profile *profile) {
    mappings_free((qa_supply_mapping *)profile->weapons, profile->weapon_count);
    mappings_free((qa_supply_mapping *)profile->ammo, profile->ammo_count);
    free((void *)profile->weapon_owners);
    free((void *)profile->ammo_owners);
    *profile = (qa_supply_profile){0};
}
static bool append_owner(qa_supply_source_owner **rows, size_t *count,
    qa_item_id item, qa_item_id source, qa_error *error) {
    if (*count == SIZE_MAX / sizeof(**rows))
        return application_fail(error, QA_ERROR_MEMORY, "Supply owners exceed native extent");
    qa_supply_source_owner *grown = realloc(*rows, (*count + 1) * sizeof(*grown));
    if (!grown) return application_fail(error, QA_ERROR_MEMORY, "Allocating supply owners");
    *rows = grown; grown[(*count)++] = (qa_supply_source_owner){item, source};
    return true;
}
static bool install_owners(application_supplies *owner, qa_supply_source_owner **rows,
    size_t *count, const supply_alias *source, size_t source_count, qa_error *error) {
    for (size_t i = 0; i < source_count; ++i) {
        qa_item_id item, base;
        if (!intern(owner, source[i].item, &item, error) ||
            !intern(owner, source[i].base, &base, error) ||
            !append_owner(rows, count, item, base, error)) return false;
    }
    return true;
}
static bool extend_owners(application_supplies *owner, qa_supply_source_owner **rows,
    size_t *count, const supply_row *extensions, size_t extension_count, qa_error *error) {
    size_t original_count = *count;
    for (size_t i = 0; i < original_count; ++i) {
        qa_supply_source_owner original = (*rows)[i];
        for (size_t j = 0; j < extension_count; ++j) {
            qa_item_id base;
            if (!intern(owner, extensions[j].source, &base, error)) return false;
            if (original.item != base) continue;
            for (size_t k = 0; k < 4 && extensions[j].destinations[k]; ++k) {
                qa_item_id item;
                if (!intern(owner, extensions[j].destinations[k], &item, error) ||
                    !append_owner(rows, count, item, original.source, error)) return false;
            }
        }
    }
    return true;
}
static bool source_extensions(application_supplies *owner, qa_supply_mapping **ammo,
    size_t *ammo_count, qa_supply_mapping **weapons, size_t *weapon_count,
    qa_supply_source_owner **owners, size_t *owner_count, qa_error *error) {
    /* Infer ownership from the original destination rows before adding source
     * aliases, exactly as expansionSourceSupply does. */
    for (size_t i = 0; i < *ammo_count; ++i)
        for (size_t j = 0; j < (*ammo)[i].count; ++j) {
            qa_item_id item = (*ammo)[i].destinations[j];
            bool owned = false;
            for (size_t k = 0; k < *owner_count; ++k)
                if ((*owners)[k].item == item) { owned = true; break; }
            if (owned) continue;
            qa_item_id source = 0;
            size_t matches = 0;
            for (size_t k = 0; k < *ammo_count; ++k)
                for (size_t n = 0; n < (*ammo)[k].count; ++n)
                    if ((*ammo)[k].destinations[n] == item) {
                        source = (*ammo)[k].source; ++matches; break;
                    }
            if (matches == 1 && !append_owner(owners, owner_count, item, source, error))
                return false;
        }
    return alias_rows(owner, ammo, ammo_count, ammo_aliases,
        sizeof(ammo_aliases) / sizeof(*ammo_aliases), error) &&
        alias_rows(owner, weapons, weapon_count, weapon_aliases,
            sizeof(weapon_aliases) / sizeof(*weapon_aliases), error);
}
static bool hipnotic_rows(application_supplies *owner, qa_supply_mapping *weapons,
    size_t count, const supply_row *extensions, size_t extension_count,
    qa_supply_source_owner **owners, size_t *owner_count, bool ownership, qa_error *error) {
    for (size_t i = 0; i < extension_count; ++i) {
        qa_item_id source;
        if (!intern(owner, extensions[i].source, &source, error)) return false;
        qa_supply_mapping *row = mapping(weapons, count, source);
        if (!row) return application_fail(error, QA_ERROR_NOT_FOUND, "Hipnotic source mapping is absent");
        for (size_t j = 0; j < 4 && extensions[i].destinations[j]; ++j) {
            qa_item_id item;
            if (!intern(owner, extensions[i].destinations[j], &item, error) ||
                !add_destination(row, item, error) ||
                (ownership && !append_owner(owners, owner_count, item, source, error))) return false;
        }
    }
    return true;
}
typedef struct supply_expansion {
    const char *name;
    const supply_row *ammo, *weapons;
    size_t ammo_count, weapon_count;
    const supply_alias *periodic;
    size_t periodic_count;
} supply_expansion;
static const supply_expansion expansions[] = {
    {"q1-rogue", rogue_ammo, rogue_weapons, sizeof(rogue_ammo)/sizeof(*rogue_ammo),
        sizeof(rogue_weapons)/sizeof(*rogue_weapons), rogue_periodic,
        sizeof(rogue_periodic)/sizeof(*rogue_periodic)},
    {"q1-mg3", NULL, mg3_weapons, 0, sizeof(mg3_weapons)/sizeof(*mg3_weapons), NULL, 0},
    {"q2-xatrix", xatrix_ammo, xatrix_weapons, sizeof(xatrix_ammo)/sizeof(*xatrix_ammo),
        sizeof(xatrix_weapons)/sizeof(*xatrix_weapons), xatrix_periodic,
        sizeof(xatrix_periodic)/sizeof(*xatrix_periodic)},
    {"q2-rogue", q2_rogue_ammo, q2_rogue_weapons,
        sizeof(q2_rogue_ammo)/sizeof(*q2_rogue_ammo), sizeof(q2_rogue_weapons)/sizeof(*q2_rogue_weapons),
        q2_rogue_periodic, sizeof(q2_rogue_periodic)/sizeof(*q2_rogue_periodic)},
    {"q3-missionpack", team_ammo, team_weapons, sizeof(team_ammo)/sizeof(*team_ammo),
        sizeof(team_weapons)/sizeof(*team_weapons), team_periodic,
        sizeof(team_periodic)/sizeof(*team_periodic)}
};
static bool profile_build(supply_pair *pair, qa_error *error) {
    application_supplies *owner = pair->owner;
    qa_game_family source = pair->source->product->family, target = pair->arsenal->product->family;
    const supply_row *ammo = NULL, *weapons = NULL;
    size_t ammo_count = 0, weapon_count = 0;
    const supply_alias *weapon_owners = NULL, *ammo_owners = NULL;
    size_t weapon_owner_count = 0, ammo_owner_count = 0;
    const char *name = NULL;
    bool identity = source == target;
    if (source == QA_GAME_Q1 && target == QA_GAME_Q1) {
        ammo = q1_q3_ammo; ammo_count = sizeof(q1_q3_ammo)/sizeof(*q1_q3_ammo);
        weapons = q1_q3_weapons; weapon_count = sizeof(q1_q3_weapons)/sizeof(*q1_q3_weapons);
        name = "composition:q1-q1-supply";
    } else if (source == QA_GAME_Q1 && target == QA_GAME_Q2) {
        ammo = q1_q2_ammo; ammo_count = sizeof(q1_q2_ammo)/sizeof(*q1_q2_ammo);
        weapons = q1_q2_weapons; weapon_count = sizeof(q1_q2_weapons)/sizeof(*q1_q2_weapons);
        name = "composition:q1-base-q2-supply";
    } else if (source == QA_GAME_Q1 && target == QA_GAME_Q3) {
        ammo = q1_q3_ammo; ammo_count = sizeof(q1_q3_ammo)/sizeof(*q1_q3_ammo);
        weapons = q1_q3_weapons; weapon_count = sizeof(q1_q3_weapons)/sizeof(*q1_q3_weapons);
        name = "composition:q1-base-q3-supply";
    } else if (source == QA_GAME_Q2 && target == QA_GAME_Q1) {
        ammo = q2_q1_ammo; ammo_count = sizeof(q2_q1_ammo)/sizeof(*q2_q1_ammo);
        weapons = q2_q1_weapons; weapon_count = sizeof(q2_q1_weapons)/sizeof(*q2_q1_weapons);
        weapon_owners = q2_q1_owners; weapon_owner_count = sizeof(q2_q1_owners)/sizeof(*q2_q1_owners);
        name = "composition:q2-base-q1-supply";
    } else if (source == QA_GAME_Q2 && target == QA_GAME_Q2) {
        ammo = q2_identity_ammo; ammo_count = sizeof(q2_identity_ammo)/sizeof(*q2_identity_ammo);
        weapons = q2_q1_weapons; weapon_count = sizeof(q2_q1_weapons)/sizeof(*q2_q1_weapons);
        name = "composition:q2-base-q2-supply";
    } else if (source == QA_GAME_Q2 && target == QA_GAME_Q3) {
        ammo = q2_q3_ammo; ammo_count = sizeof(q2_q3_ammo)/sizeof(*q2_q3_ammo);
        weapons = q2_q3_weapons; weapon_count = sizeof(q2_q3_weapons)/sizeof(*q2_q3_weapons);
        weapon_owners = q2_q3_owners; weapon_owner_count = sizeof(q2_q3_owners)/sizeof(*q2_q3_owners);
        name = "composition:q2-base-q3-supply";
    } else if (source == QA_GAME_Q3 && target == QA_GAME_Q1) {
        ammo = q3_q1_ammo; ammo_count = sizeof(q3_q1_ammo)/sizeof(*q3_q1_ammo);
        weapons = q3_q1_weapons; weapon_count = sizeof(q3_q1_weapons)/sizeof(*q3_q1_weapons);
        ammo_owners = q3_q1_owners; ammo_owner_count = sizeof(q3_q1_owners)/sizeof(*q3_q1_owners);
        name = "composition:q3-base-q1-supply";
    } else if (source == QA_GAME_Q3 && target == QA_GAME_Q2) {
        ammo = q3_q2_ammo; ammo_count = sizeof(q3_q2_ammo)/sizeof(*q3_q2_ammo);
        weapons = q3_q2_weapons; weapon_count = sizeof(q3_q2_weapons)/sizeof(*q3_q2_weapons);
        ammo_owners = q3_q2_owners; ammo_owner_count = sizeof(q3_q2_owners)/sizeof(*q3_q2_owners);
        name = "composition:q3-base-q2-supply";
    } else if (source == QA_GAME_Q3 && target == QA_GAME_Q3) {
        ammo = q3_identity_ammo; ammo_count = sizeof(q3_identity_ammo)/sizeof(*q3_identity_ammo);
        weapons = q3_q2_weapons; weapon_count = sizeof(q3_q2_weapons)/sizeof(*q3_q2_weapons);
        name = "composition:q3-base-q3-supply";
    } else return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected supply profile is not implemented");

    qa_supply_mapping *owned_ammo = NULL, *owned_weapons = NULL;
    qa_supply_source_owner *owned_ammo_owners = NULL, *owned_weapon_owners = NULL;
    qa_supply_profile profile = {0};
    bool ok = install_rows(owner, &owned_ammo, &profile.ammo_count, ammo, ammo_count, identity, error) &&
        install_rows(owner, &owned_weapons, &profile.weapon_count, weapons, weapon_count, identity, error) &&
        install_owners(owner, &owned_ammo_owners, &profile.ammo_owner_count, ammo_owners, ammo_owner_count, error) &&
        install_owners(owner, &owned_weapon_owners, &profile.weapon_owner_count, weapon_owners, weapon_owner_count, error);
    if (ok && source == QA_GAME_Q3 && target == QA_GAME_Q3) {
        qa_item_id grapple;
        ok = intern(owner, "q3:weapon/grapple", &grapple, error) &&
            append_mapping(&owned_weapons, &profile.weapon_count, grapple, &grapple, 1, error);
        for (size_t i = 0; ok && i < profile.ammo_count; ++i)
            ok = append_owner(&owned_ammo_owners, &profile.ammo_owner_count,
                owned_ammo[i].source, owned_ammo[i].source, error);
        ammo_owner_count = profile.ammo_owner_count;
    }
    const char *campaign = pair->arsenal->product->campaign;
    if (ok && target == QA_GAME_Q1 && !strcmp(campaign, "hipnotic")) {
        const supply_row *rows = source == QA_GAME_Q1 ? q1_hipnotic_weapons :
            source == QA_GAME_Q2 ? q2_hipnotic_weapons : q3_hipnotic_weapons;
        size_t count = source == QA_GAME_Q1 ? sizeof(q1_hipnotic_weapons)/sizeof(*q1_hipnotic_weapons) :
            source == QA_GAME_Q2 ? sizeof(q2_hipnotic_weapons)/sizeof(*q2_hipnotic_weapons) :
            sizeof(q3_hipnotic_weapons)/sizeof(*q3_hipnotic_weapons);
        ok = hipnotic_rows(owner, owned_weapons, profile.weapon_count, rows, count,
            &owned_weapon_owners, &profile.weapon_owner_count, source == QA_GAME_Q2, error);
        name = source == QA_GAME_Q1 ? "composition:q1-hipnotic-supply" :
            source == QA_GAME_Q2 ? "composition:q2-hipnotic-supply" : "composition:q3-hipnotic-supply";
    }
    size_t selected[2], selected_count = 0;
    if (target == QA_GAME_Q1) {
        if (!strcmp(campaign, "rogue")) selected[selected_count++] = 0;
        else if (!strcmp(campaign, "mg3")) selected[selected_count++] = 1;
    } else if (target == QA_GAME_Q2) {
        if (pair->arsenal->product->edition == QA_EDITION_RERELEASE) {
            selected[selected_count++] = 2; selected[selected_count++] = 3;
        } else if (!strcmp(campaign, "xatrix")) selected[selected_count++] = 2;
        else if (!strcmp(campaign, "rogue")) selected[selected_count++] = 3;
    } else if (!strcmp(campaign, "missionpack")) selected[selected_count++] = 4;
    char profile_name[160];
    int length = snprintf(profile_name, sizeof(profile_name), "%s", name);
    if (length < 0 || (size_t)length >= sizeof(profile_name))
        ok = application_fail(error, QA_ERROR_MEMORY, "Supply profile identity exceeds native extent");
    for (size_t i = 0; ok && i < selected_count; ++i) {
        const supply_expansion *extension = expansions + selected[i];
        ok = expand_rows(owner, owned_ammo, profile.ammo_count, extension->ammo, extension->ammo_count, error) &&
            expand_rows(owner, owned_weapons, profile.weapon_count, extension->weapons, extension->weapon_count, error) &&
            (weapon_owner_count == 0 || extend_owners(owner, &owned_weapon_owners,
                &profile.weapon_owner_count, extension->weapons, extension->weapon_count, error)) &&
            (ammo_owner_count == 0 || install_owners(owner, &owned_ammo_owners,
                &profile.ammo_owner_count, extension->periodic, extension->periodic_count, error));
        if (ok) {
            int appended = snprintf(profile_name + length, sizeof(profile_name) - (size_t)length,
                "%s%s", i ? "+" : "/", extension->name);
            if (appended < 0 || (size_t)appended >= sizeof(profile_name) - (size_t)length)
                ok = application_fail(error, QA_ERROR_MEMORY, "Supply profile identity exceeds native extent");
            else length += appended;
        }
    }
    if (ok) {
        int appended = snprintf(profile_name + length, sizeof(profile_name) - (size_t)length,
            "%s/expansion-sources", selected_count || target == QA_GAME_Q3 ? "" : "/");
        if (appended < 0 || (size_t)appended >= sizeof(profile_name) - (size_t)length)
            ok = application_fail(error, QA_ERROR_MEMORY, "Supply profile identity exceeds native extent");
    }
    if (ok) ok = source_extensions(owner, &owned_ammo, &profile.ammo_count,
        &owned_weapons, &profile.weapon_count, &owned_ammo_owners, &profile.ammo_owner_count, error) &&
        intern(owner, profile_name, &pair->profile_id, error);
    profile.ammo = owned_ammo; profile.weapons = owned_weapons;
    profile.ammo_owners = owned_ammo_owners; profile.weapon_owners = owned_weapon_owners;
    if (!ok) { profile_free(&profile); return false; }
    pair->profile = profile; return true;
}

static bool provider_current(const application_supplies *owner,
    const application_provider *provider) {
    return provider && provider->application == owner->application && provider->launch &&
        provider->product && provider->constructed && provider->attached && !provider->close_pending;
}
static supply_pair *pair_find(application_supplies *owner, application_provider *source,
    application_provider *arsenal) {
    for (supply_pair *pair = owner->pairs; pair; pair = pair->next)
        if (pair->source == source && pair->arsenal == arsenal) return pair;
    return NULL;
}
static supply_actor *actor_find(application_supplies *owner, supply_pair *pair, qa_actor_id actor) {
    for (supply_actor *entry = owner->actors; entry; entry = entry->next)
        if (entry->pair == pair && qa_actor_id_equal(entry->actor, actor)) return entry;
    return NULL;
}
static bool original_player_current(application_provider *provider, qa_actor_id actor,
    qa_error *error) {
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (engine && engine->provider == provider && engine->initialized && engine->clients &&
            provider->state.qc.instance && !engine->loading)
            for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
                const application_qc_client *client = engine->clients + slot;
                qa_qc_slot_binding binding;
                const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
                if (client->connected && qa_actor_id_equal(client->actor, actor) && record &&
                    qa_qc_slot(provider->state.qc.instance, slot, &binding) &&
                    qa_actor_id_equal(binding.actor, actor) && binding.owner == record->owner &&
                    binding.source_slot == (record->has_source ? record->source_slot : 0) &&
                    (binding.kind == QA_QC_SLOT_BORROWED || binding.kind == QA_QC_SLOT_OWNED)) return true;
            }
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (engine->provider == provider && engine->initialized && !engine->shutting_down)
            for (size_t slot = 0; slot < sizeof(engine->clients)/sizeof(*engine->clients); ++slot) {
                const application_native_q2_client *client = engine->clients + slot;
                if (client->connected && !client->disconnect_started &&
                    qa_actor_id_equal(client->actor, actor)) return true;
            }
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        if (engine && engine->provider == provider && engine->game && engine->game->primary &&
            engine->game->initialized && !engine->game->retired && engine->game->host)
            for (size_t slot = 0; slot < sizeof(engine->clients)/sizeof(*engine->clients); ++slot) {
                const q3g_client *client = engine->clients + slot;
                uint32_t actual;
                if (client->allocated && client->connected && !client->disconnect_pending &&
                    !client->disconnect_started && !client->pending_retirement &&
                    qa_actor_id_equal(client->actor, actor) &&
                    qa_q3_host_actor_slot(engine->game->host, actor, &actual, error) && actual == slot)
                    return true;
            }
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Native source supply has no actual original player binding");
}
static bool source_player_current(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    qa_application *app = source->application;
    const struct application_player_roster *roster = app->players;
    if (!roster || roster->map_provider != source ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply source is not the actual published GAME");
    const application_player_record *record = NULL;
    for (size_t i = 0; i < roster->count; ++i)
        if (!roster->records[i].retiring && qa_actor_id_equal(roster->records[i].actor, actor)) {
            record = roster->records + i; break;
        }
    if (!record) return application_fail(error, QA_ERROR_ARGUMENT, "Supply source actor has no actual roster admission");
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_source_client_view client;
        if (qa_q1_source_client_read(source->state.q1, actor, &client) &&
            client.slot == record->client_slot) return true;
    } else if (source->kind == APPLICATION_PROVIDER_Q2) {
        qa_builtin_player_info client;
        if (qa_q2_player_projection(source->state.q2, actor, &client) &&
            client.slot == record->client_slot) return true;
    } else if (source->kind == APPLICATION_PROVIDER_Q3) {
        uint32_t slot;
        if (!qa_q3_native_client_slot(source->state.q3, actor, &slot, error)) return false;
        if (slot == record->client_slot) return true;
    } else if (source->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = source->state.qc.engine;
        uint32_t slot = record->client_slot + 1;
        if (slot && engine && slot <= engine->max_clients && engine->clients &&
            engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor))
            return original_player_current(source, actor, error);
    } else if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        struct application_native_q2 *engine = source->state.native.q2_engine;
        uint32_t slot = record->client_slot + 1;
        if (slot && slot < sizeof(engine->clients)/sizeof(*engine->clients) &&
            engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor))
            return original_player_current(source, actor, error);
    } else {
        struct application_q3_guest *engine = q3g_engine(source);
        uint32_t slot = record->client_slot;
        if (engine && slot < sizeof(engine->clients)/sizeof(*engine->clients) &&
            engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor))
            return original_player_current(source, actor, error);
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Supply actor differs from its actual physical source client");
}
static bool pair_current(supply_pair *pair, qa_actor_id actor, qa_error *error) {
    application_supplies *owner = pair->owner;
    qa_application *app = owner->application;
    if (app->supplies != owner || app->inventory != owner->inventory || app->destroy_requested ||
        app->finalizing || !provider_current(owner, pair->source) ||
        !provider_current(owner, pair->arsenal) ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != pair->arsenal)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected supply association is no longer current");
    if (!source_player_current(pair->source, actor, error)) return false;
    if (pair->native && pair->source == pair->arsenal &&
        pair->arsenal->kind != APPLICATION_PROVIDER_Q1 && pair->arsenal->kind != APPLICATION_PROVIDER_Q2 &&
        pair->arsenal->kind != APPLICATION_PROVIDER_Q3)
        return original_player_current(pair->arsenal, actor, error);
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player;
        if (qa_q1_player_read(pair->arsenal->state.q1, actor, &player)) return true;
    } else if (pair->arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        return qa_q2_weapon_read(pair->arsenal->state.q2, actor, &player, error);
    } else if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (qa_q3_player_read(pair->arsenal->state.q3, actor, &player) &&
            (player.selections & QA_Q3_ARSENAL)) return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Selected supply has no actual native arsenal actor");
}
static bool supply_current(void *opaque, qa_actor_id actor, qa_error *error) {
    supply_pair *pair = opaque;
    return pair_current(pair, actor, error) && (actor_find(pair->owner, pair, actor) != NULL ||
        application_fail(error, QA_ERROR_ARGUMENT, "Selected supply actor was not admitted"));
}
static application_q3_pickups *original_q3_pickups(const supply_pair *pair) {
    struct application_q3_guest *engine = q3g_engine(pair->source);
    return !pair->native && engine && engine->provider == pair->source && engine->game &&
        engine->game->primary && engine->game->kind == QA_QVM_GAME && engine->game->vm
        ? engine->game->pickups : NULL;
}
static bool original_q3_take(void *opaque, const qa_pickup_offer *offer,
    qa_pickup_execution *execution, qa_pickup_outcome *out, qa_error *error) {
    supply_pair *pair = opaque;
    *out = QA_PICKUP_REFUSED;
    if (!qa_pickup_current(execution)) return true;
    application_supplies *owner = pair->owner;
    if (owner->calls == SIZE_MAX || offer->source != pair->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original pickup lost its actual selected supply owner");
    ++owner->calls;
    application_q3_pickups *source = original_q3_pickups(pair);
    qa_supply_offer supplied = {0};
    qa_supply_options options = {0};
    bool accepted = false;
    bool ok = supply_current(pair, offer->recipient, error) &&
        application_q3_pickups_supply(source, offer, &supplied, &options, error);
    if (ok && supplied.kind != QA_SUPPLY_AMMO && supplied.kind != QA_SUPPLY_WEAPON)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Original QVM pickup has no static supply grant");
    if (ok) ok = qa_supply_apply(pair->supply, offer->recipient, &supplied, &options,
        &accepted, error) && supply_current(pair, offer->recipient, error);
    if (ok) *out = qa_pickup_current(execution)
        ? (accepted ? QA_PICKUP_ACCEPTED : QA_PICKUP_REFUSED) : QA_PICKUP_STALE;
    --owner->calls;
    return ok;
}
static void pickup_rules_free(supply_pair *pair) {
    for (size_t i = 0; i < pair->pickup_rule_count; ++i) {
        free((void *)pair->pickup_rules[i].offered);
        free((void *)pair->pickup_rules[i].writes);
    }
    free(pair->pickup_rules); pair->pickup_rules = NULL; pair->pickup_rule_count = 0;
}
static bool pickup_write_add(qa_pickup_write **writes, size_t *count,
    qa_item_id item, qa_error *error) {
    for (size_t i = 0; i < *count; ++i) if ((*writes)[i].resource.item == item) return true;
    if (!item || *count == SIZE_MAX / sizeof(**writes))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original pickup write set exceeds its destination extent");
    qa_pickup_write *grown = realloc(*writes, (*count + 1) * sizeof(*grown));
    if (!grown) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected original pickup destinations");
    *writes = grown; grown[(*count)++] = (qa_pickup_write){
        .resource = {.kind = QA_PICKUP_INVENTORY, .item = item}, .fields = QA_PICKUP_COUNT};
    return true;
}
static bool pickup_rules_build(supply_pair *pair, qa_error *error) {
    if (!original_q3_pickups(pair)) return true;
    const qa_supply_profile *profile = &pair->profile;
    if (profile->ammo_count > SIZE_MAX - profile->weapon_count)
        return application_fail(error, QA_ERROR_MEMORY, "Original pickup rule inventory overflows");
    size_t count = profile->ammo_count + profile->weapon_count;
    if (!count || count > SIZE_MAX / sizeof(*pair->pickup_rules))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected original pickups need their actual supply mappings");
    pair->pickup_rules = calloc(count, sizeof(*pair->pickup_rules));
    if (!pair->pickup_rules) return application_fail(error, QA_ERROR_MEMORY, "Owning selected original pickup rules");
    pair->pickup_rule_count = count;
    for (size_t i = 0; i < count; ++i) {
        bool ammo = i < profile->ammo_count;
        const qa_supply_mapping *mapping = ammo ? profile->ammo + i : profile->weapons + i - profile->ammo_count;
        qa_pickup_rule *rule = pair->pickup_rules + i;
        qa_item_id *offered = malloc(sizeof(*offered));
        if (!offered) return application_fail(error, QA_ERROR_MEMORY, "Retaining original pickup item identity");
        *offered = mapping->source; rule->offered = offered; rule->offered_count = 1;
        qa_bytes item = qa_strings_text(qa_session_strings(pair->owner->application->session), mapping->source);
        if (!item.data || item.size > SIZE_MAX - 10)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original pickup mapping has no retained item name");
        char *name = malloc(item.size + 10);
        if (!name) return application_fail(error, QA_ERROR_MEMORY, "Naming selected original pickup rule");
        memcpy(name, "selected:", 9); memcpy(name + 9, item.data, item.size); name[item.size + 9] = 0;
        bool named = intern(pair->owner, name, &rule->id, error); free(name);
        if (!named) return false;
        qa_pickup_write *writes = NULL;
        bool ok = true;
        for (size_t j = 0; ok && j < mapping->count; ++j)
            ok = pickup_write_add(&writes, &rule->write_count, mapping->destinations[j], error);
        for (size_t j = 0; ok && !ammo && j < profile->ammo_count; ++j)
            for (size_t k = 0; ok && k < profile->ammo[j].count; ++k)
                ok = pickup_write_add(&writes, &rule->write_count, profile->ammo[j].destinations[k], error);
        rule->writes = writes;
        if (!ok) return false;
        if (!rule->write_count)
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected original pickup has no destination");
        rule->context = pair; rule->take = original_q3_take;
    }
    return true;
}
static bool pickup_actor_bind(supply_actor *entry, qa_error *error) {
    supply_pair *pair = entry->pair;
    if (!pair->pickup_rule_count || entry->pickups.serial) return true;
    if (!qa_pickups_idle(pair->owner->application->pickups))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected pickup admission retains an entered grant");
    qa_actor_id actor = entry->actor;
    qa_pickup_lease lease = {0};
    if (!qa_pickups_bind(pair->owner->application->pickups, actor, pair->arsenal->owner,
        pair->pickup_rules, pair->pickup_rule_count, &lease, error)) return false;
    if (actor_find(pair->owner, pair, actor) != entry) {
        if (!qa_pickups_close(pair->owner->application->pickups, lease, error)) return false;
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected pickup recipient retired during its real inventory admission");
    }
    entry->pickups = lease;
    return pair_current(pair, actor, error);
}
static bool ammo_granted(void *, qa_actor_id, const qa_pickup_receipt *, size_t, bool, qa_error *);
static bool weapon_granted(void *, qa_actor_id, const qa_item_id *, size_t,
    qa_pickup_selection_mode, qa_error *);

static const int32_t timer_maximum[QA_Q3_WEAPON_COUNT] =
    {0, 0, 50, 10, 10, 10, 50, 10, 50, 10, 0, 10, 5, 100};
static const int32_t timer_increment[QA_Q3_WEAPON_COUNT] =
    {0, 0, 4, 1, 1, 1, 5, 1, 5, 1, 0, 1, 1, 5};
static const int32_t timer_period[QA_Q3_WEAPON_COUNT] =
    {0, 0, 1000, 1500, 2000, 1750, 1500, 1750, 1500, 4000, 0, 1250, 2000, 1000};
static qa_q3_weapon timer_weapon(const supply_pair *pair, qa_item_id source) {
    for (int weapon = 1; weapon < QA_Q3_WEAPON_COUNT; ++weapon)
        if (timer_period[weapon] &&
            qa_q3_weapon_item(pair->source->state.q3, (qa_q3_weapon)weapon, true) == source)
            return (qa_q3_weapon)weapon;
    return QA_Q3_W_NONE;
}
static bool timer_profile_valid(supply_pair *pair, qa_error *error) {
    if (pair->native || pair->source->kind != APPLICATION_PROVIDER_Q3) return true;
    for (size_t i = 0; i < pair->profile.ammo_owner_count; ++i)
        if (timer_weapon(pair, pair->profile.ammo_owners[i].source) == QA_Q3_W_NONE)
            return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo pool has no original Q3 regeneration rule");
    return true;
}

bool application_supplies_create(qa_application *app, qa_inventory *inventory,
    application_supplies **out, qa_error *error) {
    if (!app || !app->session || !inventory || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Supplies require actual application inventory");
    *out = NULL;
    application_supplies *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating application supplies");
    owner->application = app; owner->inventory = inventory; *out = owner;
    return true;
}
bool application_supplies_idle(const application_supplies *owner) {
    if (!owner) return true;
    if (owner->calls) return false;
    for (const supply_pair *pair = owner->pairs; pair; pair = pair->next)
        if (!qa_supply_idle(pair->supply) ||
            (pair->pickup_rule_count && !qa_pickups_idle(owner->application->pickups))) return false;
    return true;
}
bool application_supplies_destroy(application_supplies *owner, qa_error *error) {
    if (!application_supplies_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application supplies are held by a source operation");
    if (!owner) return true;
    while (owner->actors) {
        supply_actor *entry = owner->actors;
        if (entry->pickups.serial && !qa_pickups_close(owner->application->pickups, entry->pickups, error)) return false;
        owner->actors = entry->next;
        free(entry->timers); free(entry);
    }
    while (owner->pairs) {
        supply_pair *pair = owner->pairs; owner->pairs = pair->next;
        qa_supply_destroy(pair->supply); pickup_rules_free(pair); profile_free(&pair->profile); free(pair);
    }
    free(owner); return true;
}
bool application_supplies_prepare(application_supplies *owner, application_provider *source,
    application_provider *arsenal, qa_error *error) {
    if (!owner || !application_supplies_idle(owner) || !source || !arsenal ||
        source->application != owner->application || arsenal->application != owner->application ||
        !source->constructed || !arsenal->constructed || source->close_pending || arsenal->close_pending ||
        !source->product || !arsenal->product ||
        (source != arsenal && arsenal->kind != APPLICATION_PROVIDER_Q1 && arsenal->kind != APPLICATION_PROVIDER_Q2 &&
         arsenal->kind != APPLICATION_PROVIDER_Q3))
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply preparation needs actual source and native arsenal owners");
    if (pair_find(owner, source, arsenal)) return true;
    supply_pair *pair = calloc(1, sizeof(*pair));
    if (!pair) return application_fail(error, QA_ERROR_MEMORY, "Allocating selected supply pair");
    pair->owner = owner; pair->source = source; pair->arsenal = arsenal;
    pair->native = source == arsenal ||
        (source->kind == APPLICATION_PROVIDER_Q3 && arsenal->kind == APPLICATION_PROVIDER_Q3);
    ++owner->calls;
    bool ok = true;
    if (!pair->native) {
        qa_supply_hooks hooks = {.context = pair, .ammo_granted = ammo_granted,
            .weapon_granted = weapon_granted, .current = supply_current};
        ok = profile_build(pair, error) && timer_profile_valid(pair, error) &&
            qa_supply_create(owner->inventory, &pair->profile, &hooks, &pair->supply, error) &&
            pickup_rules_build(pair, error);
    }
    --owner->calls;
    if (!ok) { qa_supply_destroy(pair->supply); pickup_rules_free(pair); profile_free(&pair->profile); free(pair); return false; }
    pair->next = owner->pairs; owner->pairs = pair; return true;
}
static bool destination_metadata(supply_pair *pair, qa_item_id item,
    qa_inventory_entry *out, qa_error *error) {
    application_provider *providers[] = {pair->arsenal, pair->source};
    for (size_t owner = 0; owner < sizeof(providers)/sizeof(*providers); ++owner) {
        application_provider *provider = providers[owner];
        if (provider->kind != APPLICATION_PROVIDER_Q2) continue;
        for (size_t i = 0; i < qa_q2_item_count(provider->state.q2); ++i) {
            const qa_q2_item_definition *definition = qa_q2_item_at(provider->state.q2, i);
            if (definition && definition->item == item) {
                *out = (qa_inventory_entry){.item = item, .capacity = definition->capacity,
                    .policy = QA_COUNT_SOURCE_INT32};
                return true;
            }
        }
    }
    const char *name = qa_strings_cstr(qa_session_strings(pair->owner->application->session), item);
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Supply %s -> %s lacks native inventory for %s",
        pair->source->product->key, pair->arsenal->product->key, name ? name : "<unnamed>");
    return false;
}
static bool admission_entries(supply_pair *pair, qa_actor_id actor,
    qa_inventory_entry **out, size_t *count, qa_error *error) {
    *out = NULL; *count = 0;
    qa_inventory_entry *current = NULL;
    size_t current_count = 0;
    if (!qa_inventory_entries(pair->owner->inventory, actor, NULL, 0, &current_count, error) ||
        !pair_current(pair, actor, error)) return false;
    if (current_count > SIZE_MAX / sizeof(*current))
        return application_fail(error, QA_ERROR_MEMORY, "Supply inventory exceeds native extent");
    if (current_count) {
        current = malloc(current_count * sizeof(*current));
        if (!current) return application_fail(error, QA_ERROR_MEMORY, "Reading actual supply inventory");
        size_t read_count = 0;
        if (!qa_inventory_entries(pair->owner->inventory, actor, current, current_count, &read_count, error) ||
            !pair_current(pair, actor, error)) { free(current); return false; }
        current_count = read_count;
    }
    const qa_supply_mapping *lists[] = {pair->profile.weapons, pair->profile.ammo};
    size_t counts[] = {pair->profile.weapon_count, pair->profile.ammo_count};
    bool ok = true;
    for (size_t list = 0; ok && list < 2; ++list)
        for (size_t i = 0; ok && i < counts[list]; ++i)
            for (size_t j = 0; ok && j < lists[list][i].count; ++j) {
                qa_item_id item = lists[list][i].destinations[j];
                bool seen = false;
                for (size_t k = 0; k < *count; ++k) if ((*out)[k].item == item) { seen = true; break; }
                if (seen) continue;
                qa_inventory_entry entry = {0};
                bool found = false;
                for (size_t k = 0; k < current_count; ++k)
                    if (current[k].item == item) { entry = current[k]; found = true; break; }
                if (!found && !destination_metadata(pair, item, &entry, error)) { ok = false; break; }
                if (*count == SIZE_MAX / sizeof(**out)) {
                    ok = application_fail(error, QA_ERROR_MEMORY, "Supply admission exceeds native extent");
                    break;
                }
                qa_inventory_entry *grown = realloc(*out, (*count + 1) * sizeof(*grown));
                if (!grown) { ok = application_fail(error, QA_ERROR_MEMORY, "Allocating supply admission entries"); break; }
                *out = grown; grown[(*count)++] = entry;
            }
    free(current);
    return ok;
}
bool application_supplies_admit(application_supplies *owner, application_provider *source,
    qa_actor_id actor, qa_error *error) {
    if (!owner || owner->calls == SIZE_MAX || owner->admitting)
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply admission has no available owner");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair || !pair_current(pair, actor, error))
        return pair ? false : application_fail(error, QA_ERROR_ARGUMENT, "Selected supply pair was not prepared");
    supply_actor *existing = actor_find(owner, pair, actor);
    if (existing) {
        ++owner->calls; owner->admitting = true;
        bool ok = true;
        if (existing->pickups.serial) {
            if (!qa_pickups_idle(owner->application->pickups))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected pickup rebind requires its returned grant owner");
            else ok = qa_pickups_close(owner->application->pickups, existing->pickups, error);
            if (ok) { existing->pickups = (qa_pickup_lease){0}; existing->pickup_imported = false; }
        }
        if (ok) ok = pickup_actor_bind(existing, error);
        owner->admitting = false; --owner->calls;
        return ok;
    }
    supply_actor *entry = calloc(1, sizeof(*entry));
    if (!entry) return application_fail(error, QA_ERROR_MEMORY, "Allocating supply actor admission");
    entry->pair = pair; entry->actor = actor;
    ++owner->calls;
    owner->admitting = true;
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    qa_inventory_admission *admission = NULL;
    bool ok = true;
    if (!pair->native) {
        if (arsenal->kind == APPLICATION_PROVIDER_Q2)
            ok = qa_q2_items_admit_player(arsenal->state.q2, actor, false, error);
        else if (arsenal->kind == APPLICATION_PROVIDER_Q3)
            ok = qa_q3_inventory_admit(arsenal->state.q3, actor, error);
        if (ok) ok = pair_current(pair, actor, error);
    }
    if (ok) ok = pair->native || (admission_entries(pair, actor, &entries, &count, error) &&
        qa_inventory_prepare_entries(owner->inventory, actor, entries, count, &admission, error) &&
        pair_current(pair, actor, error) && qa_inventory_admission_validate(admission, error) &&
        pair_current(pair, actor, error) && qa_inventory_admission_commit(admission, error));
    if (ok) admission = NULL;
    qa_inventory_admission_abort(admission); free(entries);
    if (ok) { entry->next = owner->actors; owner->actors = entry;
        ok = pickup_actor_bind(entry, error); }
    else free(entry);
    owner->admitting = false;
    --owner->calls; return ok;
}
bool application_supplies_reconnect(application_supplies *owner, qa_error *error) {
    if (!owner || owner->admitting || !application_supplies_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply reconnect requires its returned publication owner");
    const struct application_player_roster *roster = owner->application->players;
    for (size_t i = 0; roster && i < roster->count; ++i) {
        const application_player_record *record = roster->records + i;
        if (!record->actor.registry || record->retiring ||
            application_player_qw_spectator(roster->map_provider, record)) continue;
        application_provider *arsenal = application_provider_for(owner->application,
            record->actor, QA_ROLE_ARSENAL, "");
        supply_pair *pair = pair_find(owner, roster->map_provider, arsenal);
        if (pair && actor_find(owner, pair, record->actor)) {
            if (!pair_current(pair, record->actor, error)) return false;
        } else if (!application_supplies_admit(owner, roster->map_provider,
            record->actor, error)) return false;
    }
    return true;
}
bool application_supplies_pickup_rule(application_supplies *owner,qa_actor_id actor,
    qa_actor_owner provider,uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *error) {
    if (!owner || !out || !serial || !id || owner->application->operation != APPLICATION_PERSISTING ||
        !application_supplies_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Saved selected pickup needs its returned candidate supply owner");
    application_provider *source = application_world_provider(owner->application, QA_ROLE_ENTITIES, "");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair || !arsenal || arsenal->owner != provider || !original_q3_pickups(pair) ||
        !pair_current(pair, actor, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved pickup rule differs from its actual Source and arsenal actor");
    const qa_pickup_rule *rule = NULL;
    for (size_t i = 0; i < pair->pickup_rule_count; ++i)
        if (pair->pickup_rules[i].id == id) { rule = pair->pickup_rules + i; break; }
    if (!rule) return application_fail(error, QA_ERROR_FORMAT, "Saved selected pickup has no authored mapping rule");
    supply_actor *entry = actor_find(owner, pair, actor);
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) return application_fail(error, QA_ERROR_MEMORY, "Retaining restored selected pickup recipient");
        entry->pair = pair; entry->actor = actor; entry->next = owner->actors; owner->actors = entry;
    }
    if (entry->pickup_imported && entry->pickups.serial != serial)
        return application_fail(error, QA_ERROR_FORMAT, "Saved selected pickup changed its actual registration identity");
    entry->pickups = (qa_pickup_lease){actor, serial}; entry->pickup_imported = true;
    *out = *rule;
    return true;
}
void application_supplies_actor_released(application_supplies *owner, qa_actor_record actor) {
    if (!owner) return;
    supply_actor **cursor = &owner->actors;
    while (*cursor) {
        supply_actor *entry = *cursor;
        if (qa_actor_id_equal(entry->actor, actor.id)) {
            *cursor = entry->next; free(entry->timers); free(entry);
        }
        else cursor = &entry->next;
    }
}
bool application_supplies_ammo_destination(application_supplies *owner,
    application_provider *source, qa_actor_id actor, qa_item_id item, bool *found,
    qa_error *error) {
    if (!owner || !source || !item || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Ammo destination requires its actual supply association");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair || !supply_current(pair, actor, error))
        return pair ? false : application_fail(error, QA_ERROR_ARGUMENT, "Ammo destination lost its admitted supply pair");
    bool present = false;
    for (size_t i = 0; i < pair->profile.ammo_count; ++i)
        for (size_t j = 0; j < pair->profile.ammo[i].count; ++j)
            if (pair->profile.ammo[i].destinations[j] == item) present = true;
    *found = present;
    return true;
}
bool application_supplies_for(application_supplies *owner, application_provider *source,
    qa_actor_id actor, qa_supply **out, qa_error *error) {
    if (!owner || !source || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply lookup needs actual source and output");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_ARGUMENT, "Actual source supply pair is absent");
    if (!supply_current(pair, actor, error)) return false;
    *out = pair->supply; return true;
}
bool application_supplies_weapon_sources(application_supplies *owner, application_provider *source,
    qa_actor_id actor, const qa_supply_weapon *selected, size_t selected_count,
    const qa_supply_weapon *original, size_t original_count, qa_item_id *out, qa_error *error) {
    if (!owner || !source || owner->calls == SIZE_MAX || (selected_count && (!selected || !out)) ||
        (original_count && !original))
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon source mapping needs its actual supply owner and rosters");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_NOT_FOUND, "Weapon source mapping lost its prepared supply pair");
    ++owner->calls;
    bool ok = supply_current(pair, actor, error) &&
        qa_supply_weapon_sources(&pair->profile, selected, selected_count,
            original, original_count, out, error) && supply_current(pair, actor, error);
    --owner->calls; return ok;
}
bool application_supplies_source_for(void *opaque, qa_actor_id actor, qa_supply **out,
    qa_error *error) {
    application_provider *provider = opaque;
    qa_application *app = provider ? provider->application : NULL;
    application_supplies *owner = app ? app->supplies : NULL;
    if (!out || !owner || owner->calls == SIZE_MAX || app->destroy_requested || app->finalizing ||
        owner->inventory != app->inventory || !provider_current(owner, provider) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source supply lookup has no current actual provider and actor");
    if (application_world_provider(app, QA_ROLE_ENTITIES, "") == provider)
        return application_supplies_for(owner, provider, actor, out, error);
    /* A secondary arsenal grants its own declared items. Only the physical
     * GAME source uses the selected source-to-arsenal mapping. */
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player;
        if (!qa_q1_player_read(provider->state.q1, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "Secondary Q1 supply lost its actual player");
    } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_builtin_player_info player;
        qa_q2_weapon_state weapon;
        if (!qa_q2_player_projection(provider->state.q2, actor, &player) &&
            !qa_q2_weapon_read(provider->state.q2, actor, &weapon, error)) return false;
    } else if (!original_player_current(provider, actor, error)) return false;
    *out = NULL;
    return true;
}

static qa_item_id existing_item(const application_supplies *owner, const char *text) {
    return qa_strings_find(qa_session_strings(owner->application->session),
        (qa_bytes){(const uint8_t *)text, strlen(text)});
}
static const char *const q2_pickup_order[] = {
    "q2:weapon_blaster", "q2:weapon_chainfist", "q2:weapon_shotgun", "q2:weapon_supershotgun",
    "q2:weapon_machinegun", "q2:weapon_etf_rifle", "q2:weapon_chaingun", "q2:ammo_grenades",
    "q2:ammo_trap", "q2:ammo_tesla", "q2:weapon_grenadelauncher", "q2:weapon_proxlauncher",
    "q2:weapon_rocketlauncher", "q2:weapon_hyperblaster", "q2:weapon_boomer",
    "q2:weapon_plasmabeam", "q2:weapon_railgun", "q2:weapon_phalanx",
    "q2:weapon_disintegrator", "q2:weapon_bfg"
};
static int q2_rank(supply_pair *pair, qa_item_id item) {
    if (!item) return -1;
    for (size_t i = 0; i < sizeof(q2_pickup_order)/sizeof(*q2_pickup_order); ++i)
        if (existing_item(pair->owner, q2_pickup_order[i]) == item) return (int)i;
    return -1;
}
static qa_item_id q2_weapon_item(supply_pair *pair, qa_q2_weapon weapon) {
    const qa_q2_weapon_definition *definition =
        qa_q2_weapon_definition_at(pair->arsenal->state.q2, weapon);
    return definition ? existing_item(pair->owner, definition->item) : 0;
}
static bool arsenal_item(supply_pair *pair, qa_item_id item) {
    application_provider *arsenal = pair->arsenal;
    if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        for (int weapon = QA_Q1_AXE; weapon < QA_Q1_WEAPON_COUNT; ++weapon)
            if (qa_q1_weapon_item(arsenal->state.q1, (qa_q1_weapon)weapon) == item) return true;
        for (int ammo = QA_Q1_SHELLS; ammo < QA_Q1_AMMO_COUNT; ++ammo)
            if (qa_q1_ammo_item(arsenal->state.q1, (qa_q1_ammo)ammo) == item) return true;
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        for (int weapon = QA_Q2_BLASTER; weapon < QA_Q2_WEAPON_COUNT; ++weapon) {
            const qa_q2_weapon_definition *definition =
                qa_q2_weapon_definition_at(arsenal->state.q2, (qa_q2_weapon)weapon);
            if (definition && (existing_item(pair->owner, definition->item) == item ||
                (definition->ammo && existing_item(pair->owner, definition->ammo) == item))) return true;
        }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q3) {
        int limit = !strcmp(arsenal->product->campaign, "missionpack")
            ? QA_Q3_WEAPON_COUNT : QA_Q3_W_GRAPPLE + 1;
        for (int weapon = QA_Q3_W_GAUNTLET; weapon < limit; ++weapon)
            if (qa_q3_weapon_item(arsenal->state.q3, (qa_q3_weapon)weapon, false) == item ||
                qa_q3_weapon_item(arsenal->state.q3, (qa_q3_weapon)weapon, true) == item) return true;
    }
    return false;
}
static bool source_item(const supply_pair *pair, qa_item_id item) {
    const qa_supply_mapping *lists[] = {pair->profile.weapons, pair->profile.ammo};
    size_t counts[] = {pair->profile.weapon_count, pair->profile.ammo_count};
    for (size_t list = 0; list < 2; ++list)
        for (size_t i = 0; i < counts[list]; ++i)
            if (lists[list][i].source == item) return true;
    return false;
}
bool application_supplies_cheat_arsenal(void *context, qa_actor_id actor,
    qa_q1_cheat_grant category, bool *handled, qa_error *error) {
    application_provider *publisher = context;
    qa_application *app = publisher ? publisher->application : NULL;
    application_supplies *owner = app ? app->supplies : NULL;
    if (!handled || !owner || owner->calls == SIZE_MAX ||
        (category != QA_Q1_CHEAT_WEAPONS && category != QA_Q1_CHEAT_AMMO) ||
        !provider_current(owner, publisher))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat grant lost its actual supplier");
    *handled = false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat grant has no admitted supply pair");
    if (!supply_current(pair, actor, error)) return false;
    if (source == arsenal) return true;
    ++owner->calls;
    size_t definition_count = 0, entry_count = 0, written = 0;
    qa_item_definition *definitions = NULL;
    qa_inventory_entry *entries = NULL;
    bool okay = qa_inventory_entries(owner->inventory, actor, NULL, 0,
        &entry_count, error) && supply_current(pair, actor, error);
    if (okay && (entry_count > SIZE_MAX / sizeof(*definitions) ||
        entry_count > SIZE_MAX / sizeof(*entries)))
        okay = application_fail(error, QA_ERROR_MEMORY, "Selected grant exceeds native inventory extent");
    if (okay) {
        definitions = entry_count ? malloc(entry_count * sizeof(*definitions)) : NULL;
        entries = entry_count ? malloc(entry_count * sizeof(*entries)) : NULL;
        if (entry_count && (!definitions || !entries))
            okay = application_fail(error, QA_ERROR_MEMORY, "Retaining actual selected arsenal grant");
    }
    if (okay) {
        okay = qa_inventory_entries(owner->inventory, actor, entries, entry_count,
            &written, error) && supply_current(pair, actor, error);
        if (okay && written > entry_count)
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Selected grant inventory changed extent");
        if (okay) entry_count = written;
    }
    for (size_t i = 0; okay && i < entry_count; ++i) {
        qa_item_definition definition;
        qa_error lookup = {0};
        bool found = qa_inventory_source_definition_read(owner->inventory, actor,
            arsenal->owner, entries[i].item, &definition, &lookup);
        okay = supply_current(pair, actor, error);
        if (!okay) break;
        if (!found) {
            if (lookup.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = lookup;
            okay = false;
            break;
        }
        definitions[definition_count++] = definition;
    }
    bool declared = false;
    for (size_t i = 0; okay && i < entry_count; ++i) {
        bool weapon = false, ammunition = false;
        for (size_t j = 0; j < definition_count; ++j) {
            const qa_item_definition *definition = definitions + j;
            if (definition->owner != arsenal->owner || !definition->weapon) continue;
            declared = true;
            weapon |= definition->item == entries[i].item;
            ammunition |= definition->ammo && definition->ammo == entries[i].item;
        }
        if (category == QA_Q1_CHEAT_WEAPONS ? !weapon : weapon || !ammunition) continue;
        qa_inventory_entry entry = entries[i];
        entry.count = category == QA_Q1_CHEAT_WEAPONS ? 1 : entry.capacity;
        okay = qa_inventory_configure(owner->inventory, actor, &entry, NULL, NULL, error) &&
            supply_current(pair, actor, error);
    }
    if (okay && !declared)
        okay = application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected cheat grant has no admitted arsenal definitions");
    if (okay) *handled = true;
    free(definitions); free(entries);
    --owner->calls;
    return okay;
}
static bool starter_count(supply_pair *pair, qa_actor_id actor, qa_item_id item,
    double count, bool clamp, qa_error *error) {
    qa_inventory_entry entry;
    if (!item || !qa_inventory_entry_read(pair->owner->inventory, actor, item, &entry, error) ||
        !supply_current(pair, actor, error)) return false;
    entry.count = clamp ? fmin(entry.capacity, count) : count;
    return qa_inventory_configure(pair->owner->inventory, actor, &entry, NULL, NULL, error) &&
        supply_current(pair, actor, error);
}
bool application_supplies_spawn(application_supplies *owner, application_provider *source,
    qa_actor_id actor, qa_error *error) {
    if (!owner || owner->calls == SIZE_MAX || !source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal spawn lost its source owner");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair || !supply_current(pair, actor, error))
        return pair ? false : application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal spawn has no admitted source pair");
    bool original_q1 = source->kind == APPLICATION_PROVIDER_QC &&
        source->product->family == QA_GAME_Q1 && arsenal->kind == APPLICATION_PROVIDER_Q1;
    if (source == arsenal || (source->kind == APPLICATION_PROVIDER_Q3 &&
        arsenal->kind == APPLICATION_PROVIDER_Q3))
        return true;
    qa_game_family family = source->product->family;
    if (family == QA_GAME_Q1 && arsenal->kind == APPLICATION_PROVIDER_Q1) {
        if (!original_q1) {
            qa_q1_weapon weapon;
            float maximum;
            qa_q1_auto_switch preference;
            ++owner->calls;
            bool ok = source->kind == APPLICATION_PROVIDER_Q1 &&
                qa_q1_source_arsenal_spawn_read(source->state.q1, actor, &weapon,
                    &maximum, &preference, error) && supply_current(pair, actor, error);
            if (!ok && source->kind != APPLICATION_PROVIDER_Q1)
                application_fail(error, QA_ERROR_UNSUPPORTED,
                    "Q1 source spawn has no actual native or original player declaration");
            if (ok) ok = qa_q1_selected_arsenal_spawn(arsenal->state.q1, actor,
                weapon, maximum, &preference, error) && supply_current(pair, actor, error);
            --owner->calls;
            return ok;
        }
        ++owner->calls;
        qa_item_id active;
        int32_t reference;
        float maximum;
        bool ok = application_guest_weapon_read(source, actor, &active, error) &&
            supply_current(pair, actor, error) &&
            application_qc_reference(source->state.qc.engine, actor, &reference, error) &&
            application_qc_float(source->state.qc.engine, reference, "max_health", &maximum, error) &&
            supply_current(pair, actor, error);
        qa_q1_weapon weapon = QA_Q1_WEAPON_COUNT;
        for (int i = QA_Q1_AXE; ok && i < QA_Q1_WEAPON_COUNT; ++i)
            if (qa_q1_weapon_item(arsenal->state.q1, (qa_q1_weapon)i) == active) {
                weapon = (qa_q1_weapon)i; break;
            }
        if (ok && weapon == QA_Q1_WEAPON_COUNT)
            ok = application_fail(error, QA_ERROR_UNSUPPORTED,
                "Original QC weapon has no selected Q1 source definition");
        if (ok) ok = qa_q1_selected_arsenal_spawn(arsenal->state.q1, actor, weapon,
            maximum, NULL, error) && supply_current(pair, actor, error);
        --owner->calls;
        return ok;
    }
    ++owner->calls;
    bool ok = true;
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    ok = qa_inventory_entries(owner->inventory, actor, NULL, 0, &count, error) &&
        supply_current(pair, actor, error);
    if (ok && count > SIZE_MAX / sizeof(*entries))
        ok = application_fail(error, QA_ERROR_MEMORY, "Selected arsenal reset exceeds native extent");
    if (ok && count) {
        entries = malloc(count * sizeof(*entries));
        if (!entries) ok = application_fail(error, QA_ERROR_MEMORY, "Reading selected arsenal reset entries");
    }
    size_t read_count = 0;
    if (ok) ok = qa_inventory_entries(owner->inventory, actor, entries, count, &read_count, error) &&
        supply_current(pair, actor, error);
    if (ok && read_count > count)
        ok = application_fail(error, QA_ERROR_ARGUMENT,
            "Selected arsenal reset changed its retained inventory extent");
    for (size_t i = 0; ok && i < read_count; ++i) {
        bool selected = arsenal_item(pair, entries[i].item);
        bool clear = selected && (arsenal->kind == APPLICATION_PROVIDER_Q3 ||
            (arsenal->kind == APPLICATION_PROVIDER_Q2 && family != QA_GAME_Q2));
        clear |= source->kind <= APPLICATION_PROVIDER_Q3 &&
            source_item(pair, entries[i].item) && !selected;
        if (clear) ok = starter_count(pair, actor, entries[i].item, 0, false, error);
    }
    free(entries);
    if (ok && arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view initialized;
        ok = qa_q1_player_inventory_initialize(arsenal->state.q1, actor, error) &&
            supply_current(pair, actor, error);
        if (ok && !qa_q1_player_read(arsenal->state.q1, actor, &initialized))
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Initialized Q1 arsenal lost its actual source player");
        if (ok) ok = supply_current(pair, actor, error) &&
            qa_q1_selected_arsenal_spawn(arsenal->state.q1, actor, QA_Q1_SHOTGUN,
                initialized.max_health, NULL, error) && supply_current(pair, actor, error);
    } else if (ok && arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon active = family == QA_GAME_Q1 ? QA_Q2_SHOTGUN :
            family == QA_GAME_Q3 ? QA_Q2_MACHINEGUN : QA_Q2_BLASTER;
        qa_item_id blaster = q2_weapon_item(pair, QA_Q2_BLASTER);
        if (family == QA_GAME_Q2) {
            double owned;
            ok = qa_inventory_count_read(owner->inventory, actor, blaster, &owned, error) &&
                supply_current(pair, actor, error);
            if (ok && owned == 0)
                ok = qa_inventory_give(owner->inventory, actor, blaster, 1, NULL, error) &&
                    supply_current(pair, actor, error);
        } else ok = starter_count(pair, actor, blaster, 1, true, error);
        if (ok && family == QA_GAME_Q1)
            ok = starter_count(pair, actor, q2_weapon_item(pair, QA_Q2_SHOTGUN), 1, true, error) &&
                starter_count(pair, actor, existing_item(owner, "q2:ammo_shells"), 25, true, error);
        if (ok && family == QA_GAME_Q3)
            ok = starter_count(pair, actor, q2_weapon_item(pair, QA_Q2_MACHINEGUN), 1, true, error) &&
                starter_count(pair, actor, q2_weapon_item(pair, QA_Q2_CHAINGUN), 1, true, error) &&
                starter_count(pair, actor, existing_item(owner, "q2:ammo_bullets"), 100, true, error);
        double active_count;
        if (ok) ok = qa_inventory_count_read(owner->inventory, actor,
            q2_weapon_item(pair, active), &active_count, error) && supply_current(pair, actor, error);
        if (ok && active_count < 1)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected Q2 starter weapon is not owned");
        if (ok) ok = qa_q2_weapon_restore(arsenal->state.q2, actor,
            &(qa_q2_weapon_state){.weapon = active, .phase = QA_Q2_ACTIVATING,
                .gun_rate = 10, .kick_seconds = 0.2f}, error) && supply_current(pair, actor, error) &&
            qa_q2_clear_input(arsenal->state.q2, actor, error) && supply_current(pair, actor, error);
    } else if (ok && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_weapon active = family == QA_GAME_Q1 ? QA_Q3_W_SHOTGUN : QA_Q3_W_MACHINEGUN;
        ok = starter_count(pair, actor, qa_q3_weapon_item(arsenal->state.q3, QA_Q3_W_GAUNTLET, false),
            1, false, error) &&
            starter_count(pair, actor, qa_q3_weapon_item(arsenal->state.q3, active, false), 1, false, error) &&
            starter_count(pair, actor, qa_q3_weapon_item(arsenal->state.q3, active, true),
                family == QA_GAME_Q1 ? 25 : 100, false, error);
        qa_q3_player_state player;
        if (ok && !qa_q3_player_read(arsenal->state.q3, actor, &player))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 starter lost its actual arsenal state");
        if (ok) {
            player.weapon = player.requested_weapon = active;
            player.weapon_phase = QA_Q3_READY;
            player.weapon_time_ms = 0;
            player.external_slot = QA_Q3_SLOT_ACTIVE;
            player.fractional_weapon_ms = 0;
            player.has_last_fire = false;
            player.last_fire_ms = 0;
            player.respawned = true;
            player.use_item_held = false;
            ok = qa_q3_player_restore(arsenal->state.q3, actor, &player, error) &&
                supply_current(pair, actor, error);
        }
    }
    --owner->calls;
    return ok;
}
bool application_supplies_source_spawned(void *opaque, qa_actor_id actor, qa_error *error) {
    application_provider *source = opaque;
    qa_application *app = source ? source->application : NULL;
    application_supplies *owner = app ? app->supplies : NULL;
    if (!owner || !provider_current(owner, source) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source spawn lost its actual supply owner");
    if (application_world_provider(app, QA_ROLE_ENTITIES, "") != source)
        return true;
    return application_supplies_admit(owner, source, actor, error) &&
        application_supplies_spawn(owner, source, actor, error);
}
static bool q2_weapon_receipts(supply_pair *pair, qa_actor_id actor,
    const qa_item_id *items, size_t count, qa_pickup_selection_mode selection, qa_error *error) {
    if (selection == QA_PICKUP_SWITCH_NEVER) return true;
    for (size_t i = 0; i < count; ++i) {
        if (!supply_current(pair, actor, error)) return false;
        qa_q2_weapon_state state;
        if (!qa_q2_weapon_read(pair->arsenal->state.q2, actor, &state, error)) return false;
        qa_q2_weapon incoming = QA_Q2_WEAPON_NONE;
        for (int weapon = QA_Q2_BLASTER; weapon < QA_Q2_WEAPON_COUNT; ++weapon)
            if (q2_weapon_item(pair, (qa_q2_weapon)weapon) == items[i]) {
                incoming = (qa_q2_weapon)weapon; break;
            }
        if (incoming == QA_Q2_WEAPON_NONE)
            return application_fail(error, QA_ERROR_ARGUMENT, "Pickup receipt has no actual Q2 weapon definition");
        qa_item_id current = q2_weapon_item(pair,
            state.pending != QA_Q2_WEAPON_NONE ? state.pending : state.weapon);
        if (selection == QA_PICKUP_SWITCH_ALWAYS || q2_rank(pair, items[i]) > q2_rank(pair, current)) {
            qa_q2_selection selected;
            if (!qa_q2_weapon_select(pair->arsenal->state.q2, actor, incoming, false, &selected, error) ||
                !supply_current(pair, actor, error)) return false;
        }
    }
    return true;
}
static int q3_weapon_limit(const supply_pair *pair) {
    return !strcmp(pair->arsenal->product->campaign, "missionpack")
        ? QA_Q3_WEAPON_COUNT : QA_Q3_W_GRAPPLE + 1;
}
static bool q3_best_weapon(supply_pair *pair, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, qa_q3_weapon *out, qa_error *error) {
    *out = QA_Q3_W_NONE;
    for (int weapon = q3_weapon_limit(pair) - 1; weapon > QA_Q3_W_NONE; --weapon) {
        qa_item_id item = qa_q3_weapon_item(pair->arsenal->state.q3, (qa_q3_weapon)weapon, false);
        double owned;
        if (!qa_inventory_count_read(pair->owner->inventory, actor, item, &owned, error) ||
            !supply_current(pair, actor, error)) return false;
        if (owned <= 0) continue;
        qa_item_id ammo = qa_q3_weapon_item(pair->arsenal->state.q3, (qa_q3_weapon)weapon, true);
        if (ammo) {
            double available = 0; bool found = false;
            for (size_t i = 0; i < count; ++i)
                if (receipts[i].item == ammo) { available = receipts[i].before; found = true; break; }
            if (!found && (!qa_inventory_count_read(pair->owner->inventory, actor, ammo, &available, error) ||
                !supply_current(pair, actor, error))) return false;
            if (available <= 0) continue;
        }
        *out = (qa_q3_weapon)weapon; break;
    }
    return true;
}
static bool q3_weapon_receipts(supply_pair *pair, qa_actor_id actor,
    const qa_item_id *items, size_t count, qa_pickup_selection_mode selection, qa_error *error) {
    if (selection == QA_PICKUP_SWITCH_NEVER) return true;
    qa_q3_player_state player;
    if (!qa_q3_player_read(pair->arsenal->state.q3, actor, &player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 pickup receipt lost its arsenal actor");
    qa_q3_weapon next = QA_Q3_W_NONE;
    for (int weapon = q3_weapon_limit(pair) - 1; next == QA_Q3_W_NONE && weapon > QA_Q3_W_NONE; --weapon) {
        qa_item_id item = qa_q3_weapon_item(pair->arsenal->state.q3, (qa_q3_weapon)weapon, false);
        for (size_t i = 0; i < count; ++i)
            if (items[i] == item) { next = (qa_q3_weapon)weapon; break; }
    }
    return next == QA_Q3_W_NONE || (selection != QA_PICKUP_SWITCH_ALWAYS && next <= player.weapon) ||
        (qa_q3_player_request_weapon(pair->arsenal->state.q3, actor, next, error) &&
            supply_current(pair, actor, error));
}
static bool q1_source_preference(supply_pair *pair, qa_actor_id actor,
    qa_q1_auto_switch *value, const qa_q1_auto_switch **out, qa_error *error) {
    *out = NULL;
    if (pair->source->kind != APPLICATION_PROVIDER_Q1) return true;
    if (!qa_q1_player_auto_switch_read(pair->source->state.q1, actor, value, error) ||
        !supply_current(pair, actor, error)) return false;
    *out = value;
    return true;
}
static bool ammo_granted(void *context, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, bool auto_switch, qa_error *error) {
    supply_pair *pair = context;
    if (!supply_current(pair, actor, error)) return false;
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_auto_switch value;
        const qa_q1_auto_switch *preference;
        return q1_source_preference(pair, actor, &value, &preference, error) &&
            qa_q1_selected_pickup_ammo(pair->arsenal->state.q1, actor, receipts, count,
                auto_switch, preference, error) && supply_current(pair, actor, error);
    }
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_item_id grenades = existing_item(pair->owner, "q2:ammo_grenades");
        for (size_t i = 0; auto_switch && i < count; ++i)
            if (receipts[i].item == grenades && receipts[i].before == 0)
                return q2_weapon_receipts(pair, actor, &grenades, 1, QA_PICKUP_SWITCH_IF_BETTER, error);
        return true;
    }
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q1_auto_switch value;
        const qa_q1_auto_switch *preference;
        if (!q1_source_preference(pair, actor, &value, &preference, error)) return false;
        if (preference && *preference == QA_Q1_SWITCH_NEVER) auto_switch = false;
        if (!auto_switch) return true;
        qa_q3_weapon before, after;
        qa_q3_player_state player;
        if (!q3_best_weapon(pair, actor, receipts, count, &before, error)) return false;
        if (!qa_q3_player_read(pair->arsenal->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 ammunition receipt lost its actual arsenal player");
        if (before == QA_Q3_W_NONE || player.weapon != before) return true;
        if (!q3_best_weapon(pair, actor, NULL, 0, &after, error)) return false;
        return after == QA_Q3_W_NONE ||
            (qa_q3_player_request_weapon(pair->arsenal->state.q3, actor, after, error) &&
             supply_current(pair, actor, error));
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Pickup receipt has no native selected arsenal");
}
static bool weapon_granted(void *context, qa_actor_id actor,
    const qa_item_id *items, size_t count, qa_pickup_selection_mode selection, qa_error *error) {
    supply_pair *pair = context;
    if (!supply_current(pair, actor, error)) return false;
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_auto_switch value;
        const qa_q1_auto_switch *preference;
        return q1_source_preference(pair, actor, &value, &preference, error) &&
            qa_q1_selected_pickup_weapons(pair->arsenal->state.q1, actor, items, count,
                selection, preference, error) && supply_current(pair, actor, error);
    }
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q2)
        return q2_weapon_receipts(pair, actor, items, count, selection, error);
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3)
        return q3_weapon_receipts(pair, actor, items, count, selection, error);
    return application_fail(error, QA_ERROR_ARGUMENT, "Pickup receipt has no native selected arsenal");
}

typedef struct q3_supply_offer {
    supply_pair *pair;
    qa_supply_offer offer;
    qa_pickup_grant ammo;
    float respawn;
} q3_supply_offer;
static bool q3_physical_source(application_provider *source, bool *physical, qa_error *error) {
    if (!source || source->kind != APPLICATION_PROVIDER_Q3 || !source->application ||
        !source->constructed || !source->attached || source->close_pending || !source->state.q3 ||
        !source->product || source->application->destroy_requested || source->application->finalizing)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply callback lost its actual source provider");
    *physical = application_world_provider(source->application, QA_ROLE_ENTITIES, "") == source;
    return true;
}
static bool q3_descriptor_current(application_provider *source,
    const qa_q3_supply_descriptor *descriptor, qa_error *error) {
    if (!source || !descriptor || !descriptor->item || source->kind != APPLICATION_PROVIDER_Q3 ||
        !source->application || !source->constructed || !source->attached || source->close_pending ||
        !source->state.q3 || !source->product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply descriptor lacks its actual native source");
    qa_q3_item_spawn spawn;
    bool finished;
    qa_q3_native_client client;
    qa_q3_rules rules;
    if (!qa_q3_source_item_spawn_read(source->state.q3, descriptor->pickup, &spawn, &finished, error) ||
        !qa_q3_client_read(source->state.q3, descriptor->recipient, &client, error) ||
        !qa_q3_rules_read(source->state.q3, &rules, error)) return false;
    size_t count;
    const qa_q3_item *items = qa_q3_items(!strcmp(source->product->campaign, "missionpack")
        ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA, &count);
    if (!finished || !spawn.item_index || spawn.item_index >= count ||
        descriptor->item != items + spawn.item_index || descriptor->count != spawn.count ||
        descriptor->dropped != spawn.dropped || descriptor->game_type != rules.game_type ||
        memcmp(&descriptor->weapon_respawn_seconds, &rules.weapon_respawn_seconds, sizeof(float)) ||
        memcmp(&descriptor->team_weapon_respawn_seconds, &rules.team_weapon_respawn_seconds, sizeof(float)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply descriptor differs from its current source item");
    return true;
}
static bool q3_offer(application_provider *source, const qa_q3_supply_descriptor *descriptor,
    qa_q3_supply_kind *kind, q3_supply_offer *out, qa_error *error) {
    application_supplies *owner = source->application->supplies;
    qa_supply *supply;
    if (!application_supplies_for(owner, source, descriptor->recipient, &supply, error)) return false;
    *kind = QA_Q3_SUPPLY_NATIVE;
    if (!supply || (descriptor->item->kind != QA_Q3_ITEM_WEAPON &&
        descriptor->item->kind != QA_Q3_ITEM_AMMO)) return true;
    application_provider *arsenal = application_provider_for(owner->application,
        descriptor->recipient, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    *out = (q3_supply_offer){.pair = pair};
    int tag = descriptor->item->tag;
    int limit = !strcmp(source->product->campaign, "missionpack")
        ? QA_Q3_WEAPON_COUNT : QA_Q3_W_GRAPPLE + 1;
    *kind = QA_Q3_SUPPLY_REJECTED;
    if (tag <= QA_Q3_W_NONE || tag >= limit) return true;
    qa_item_id weapon = qa_q3_weapon_item(source->state.q3, (qa_q3_weapon)tag, false);
    qa_item_id ammo = qa_q3_weapon_item(source->state.q3, (qa_q3_weapon)tag, true);
    if (descriptor->item->kind == QA_Q3_ITEM_AMMO) {
        if (!ammo) return true;
        out->ammo = (qa_pickup_grant){ammo,
            descriptor->count != 0 ? descriptor->count : descriptor->item->quantity};
        out->offer = (qa_supply_offer){.kind = QA_SUPPLY_AMMO, .item = ammo,
            .ammo = &out->ammo, .ammo_count = 1};
        out->respawn = 40;
    } else {
        double current = 0;
        if (ammo) {
            const qa_supply_mapping *row = NULL;
            for (size_t i = 0; i < pair->profile.ammo_count; ++i)
                if (pair->profile.ammo[i].source == ammo) { row = pair->profile.ammo + i; break; }
            if (!row || !row->count) return true;
            if (!qa_inventory_count_read(owner->inventory, descriptor->recipient,
                row->destinations[0], &current, error) ||
                !supply_current(pair, descriptor->recipient, error)) return false;
        }
        double quantity = descriptor->count != 0 ? descriptor->count : descriptor->item->quantity;
        if (descriptor->count < 0) quantity = 0;
        else if (!descriptor->dropped && descriptor->game_type != 3)
            quantity = current < quantity ? quantity - current : 1;
        out->ammo = (qa_pickup_grant){ammo, quantity};
        out->offer = (qa_supply_offer){.kind = QA_SUPPLY_WEAPON, .item = weapon,
            .ammo = ammo ? &out->ammo : NULL, .ammo_count = ammo ? 1 : 0};
        out->respawn = descriptor->game_type == 3 ? descriptor->team_weapon_respawn_seconds
            : descriptor->weapon_respawn_seconds;
    }
    *kind = QA_Q3_SUPPLY_SELECTED; return true;
}
bool application_supplies_q3_preview(void *opaque, const qa_q3_supply_descriptor *descriptor,
    qa_q3_supply_kind *kind, qa_supply_preview_result *out, qa_error *error) {
    application_provider *source = opaque;
    if (!kind || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply preview needs outputs");
    bool physical;
    if (!q3_physical_source(source, &physical, error)) return false;
    if (!physical) { *kind = QA_Q3_SUPPLY_NATIVE; *out = (qa_supply_preview_result){0}; return true; }
    if (!q3_descriptor_current(source, descriptor, error)) return false;
    application_supplies *owner = source->application->supplies;
    if (!owner || owner->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply preview has no admitted application owner");
    ++owner->calls;
    q3_supply_offer offer = {0};
    qa_supply_preview_result result = {0};
    bool ok = q3_offer(source, descriptor, kind, &offer, error);
    if (ok && *kind == QA_Q3_SUPPLY_SELECTED)
        ok = qa_supply_preview(offer.pair->supply, descriptor->recipient, &offer.offer, false, &result, error) &&
            q3_descriptor_current(source, descriptor, error);
    if (ok) *out = result;
    else qa_supply_preview_free(&result);
    --owner->calls; return ok;
}
static bool selected_deathmatch(const application_supplies *owner, bool *out, qa_error *error) {
    const qa_launch_snapshot *snapshot = owner->application->routing_snapshot;
    if (!snapshot) snapshot = qa_configuration_current(owner->application->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!choices) return application_fail(error, QA_ERROR_ARGUMENT, "Supply selection has no current launch policy");
    *out = false;
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (choices->modes[i].rules.enabled && choices->modes[i].primary_score) {
            qa_mode_kind kind = choices->modes[i].rules.kind;
            *out = kind != QA_MODE_SINGLE_PLAYER && kind != QA_MODE_COOPERATIVE;
            break;
        }
    return true;
}
bool application_supplies_q3_take(void *opaque, const qa_q3_supply_descriptor *descriptor,
    qa_q3_supply_kind *kind, bool *accepted, float *respawn, qa_error *error) {
    application_provider *source = opaque;
    if (!kind || !accepted || !respawn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply touch needs outputs");
    *accepted = false;
    *respawn = 0;
    bool physical;
    if (!q3_physical_source(source, &physical, error)) return false;
    if (!physical) { *kind = QA_Q3_SUPPLY_NATIVE; return true; }
    if (!q3_descriptor_current(source, descriptor, error)) return false;
    application_supplies *owner = source->application->supplies;
    if (!owner || owner->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 supply touch has no admitted application owner");
    ++owner->calls;
    q3_supply_offer offer = {0};
    bool ok = q3_offer(source, descriptor, kind, &offer, error);
    *accepted = false;
    if (ok && *kind == QA_Q3_SUPPLY_SELECTED) {
        bool deathmatch;
        ok = selected_deathmatch(owner, &deathmatch, error);
        qa_supply_options options = {.auto_switch = true};
        if (ok) {
            options.selection = deathmatch ? QA_PICKUP_SWITCH_IF_BETTER : QA_PICKUP_SWITCH_ALWAYS;
            ok = qa_supply_apply(offer.pair->supply, descriptor->recipient, &offer.offer,
                &options, accepted, error) && q3_descriptor_current(source, descriptor, error);
        }
        if (ok && *accepted) *respawn = offer.respawn;
    }
    --owner->calls; return ok;
}

static bool timers_require(supply_pair *pair, qa_actor_id actor, supply_actor **out, qa_error *error) {
    supply_actor *entry = actor_find(pair->owner, pair, actor);
    if (!entry || !supply_current(pair, actor, error))
        return entry ? false : application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo actor is not admitted");
    if (entry->timers_ready) { *out = entry; return true; }
    size_t count = pair->profile.ammo_owner_count;
    if (count > SIZE_MAX / sizeof(supply_timer))
        return application_fail(error, QA_ERROR_MEMORY, "Mapped ammo timers exceed native extent");
    supply_timer *timers = count ? calloc(count, sizeof(*timers)) : NULL;
    if (count && !timers) return application_fail(error, QA_ERROR_MEMORY, "Allocating mapped ammo timers");
    for (size_t i = 0; i < count; ++i) {
        qa_supply_source_owner binding = pair->profile.ammo_owners[i];
        timers[i] = (supply_timer){.item = binding.item, .source = binding.source,
            .weapon = timer_weapon(pair, binding.source)};
    }
    entry->timers = timers; entry->timer_count = count; entry->timers_ready = true;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_inventory_entry resource;
        ok = qa_inventory_entry_read(pair->owner->inventory, actor, timers[i].item, &resource, error) &&
            supply_current(pair, actor, error) && actor_find(pair->owner, pair, actor) == entry;
    }
    if (!ok) {
        if (actor_find(pair->owner, pair, actor) == entry) {
            free(entry->timers); entry->timers = NULL; entry->timer_count = 0; entry->timers_ready = false;
        }
        return false;
    }
    *out = entry; return true;
}
static int32_t add_i32(int32_t a, int32_t b) {
    uint32_t bits = (uint32_t)a + (uint32_t)b;
    int32_t value; memcpy(&value, &bits, sizeof(value)); return value;
}
bool application_supplies_q3_ammo_regeneration(void *opaque, qa_actor_id actor,
    int32_t elapsed, bool *handled, qa_error *error) {
    application_provider *source = opaque;
    if (!source || !source->application || source->kind != APPLICATION_PROVIDER_Q3 || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo step requires its actual Q3 source");
    *handled = false;
    bool physical;
    if (!q3_physical_source(source, &physical, error)) return false;
    if (!physical) return true;
    application_supplies *owner = source->application->supplies;
    qa_supply *supply;
    if (!application_supplies_for(owner, source, actor, &supply, error)) return false;
    *handled = supply != NULL;
    if (!*handled) return true;
    if (owner->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo step owner is exhausted");
    supply_pair *pair = pair_find(owner, source,
        application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, ""));
    ++owner->calls;
    supply_actor *entry;
    bool ok = timers_require(pair, actor, &entry, error);
    size_t count = ok ? entry->timer_count : 0;
    for (size_t i = 0; ok && i < count; ++i) {
        supply_timer timer = entry->timers[i];
        qa_inventory_entry resource;
        ok = qa_inventory_entry_read(owner->inventory, actor, timer.item, &resource, error) &&
            supply_current(pair, actor, error) && actor_find(owner, pair, actor) == entry;
        if (!ok) break;
        int32_t total = add_i32(timer.elapsed, elapsed), period = timer_period[timer.weapon];
        if (resource.count >= timer_maximum[timer.weapon]) total = 0;
        if (total >= period) {
            total %= period;
            double incremented=resource.count+timer_increment[timer.weapon];
            if (incremented<=-2147483649.0 || incremented>=2147483648.0) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Ammo regeneration exceeds its native counter");
                break;
            }
            int32_t next=(int32_t)incremented;
            resource.count = next > timer_maximum[timer.weapon] ? timer_maximum[timer.weapon] : next;
            ok = qa_inventory_configure(owner->inventory, actor, &resource, NULL, NULL, error) &&
                supply_current(pair, actor, error) && actor_find(owner, pair, actor) == entry;
        }
        if (ok) entry->timers[i].elapsed = total;
    }
    --owner->calls; return ok;
}
bool application_supplies_q3_ammo_stored(void *opaque, qa_actor_id actor, qa_q3_weapon weapon,
    int32_t value, qa_error *error) {
    application_provider *source = opaque;
    if (!source || !source->application || source->kind != APPLICATION_PROVIDER_Q3 ||
        (unsigned)weapon >= QA_Q3_WEAPON_COUNT)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo write requires its actual Q3 source");
    bool physical;
    if (!q3_physical_source(source, &physical, error)) return false;
    if (!physical) return true;
    application_supplies *owner = source->application->supplies;
    if (!owner || source->application->inventory != owner->inventory || !provider_current(owner, source) ||
        !qa_actors_get(qa_session_actors(owner->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo source write is no longer current");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo source write lost its prepared pair");
    if (pair->native) return true;
    /* Source stores never construct the lazy timer owner. */
    supply_actor *entry = actor_find(owner, pair, actor);
    if (entry && entry->timers_ready)
        for (size_t i = 0; i < entry->timer_count; ++i)
            if (entry->timers[i].weapon == weapon) entry->timers[i].elapsed = value;
    return true;
}

static size_t pair_count(const application_supplies *owner) {
    size_t count = 0;
    for (const supply_pair *pair = owner->pairs; pair; pair = pair->next) ++count;
    return count;
}
static supply_pair *pair_ordinal(application_supplies *owner, size_t ordinal) {
    supply_pair *pair = owner->pairs;
    while (pair && ordinal--) pair = pair->next;
    return pair;
}
static void actor_list_free(supply_actor *head) {
    while (head) { supply_actor *next = head->next; free(head->timers); free(head); head = next; }
}
static bool roster_matches(application_supplies *owner, const supply_actor *saved, qa_error *error) {
    const struct application_player_roster *roster = owner->application->players;
    size_t expected = 0, actual = 0;
    for (const supply_actor *row = saved; row; row = row->next) {
        ++actual;
        if (!pair_current(row->pair, row->actor, error)) return false;
        for (const supply_actor *prior = saved; prior != row; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, row->actor))
                return application_fail(error, QA_ERROR_FORMAT, "Duplicate saved supply actor admission");
    }
    for (size_t i = 0; roster && i < roster->count; ++i) {
        const application_player_record *player = roster->records + i;
        if (!player->actor.registry || player->retiring) continue;
        ++expected;
        const supply_actor *row = saved;
        while (row && !qa_actor_id_equal(row->actor, player->actor)) row = row->next;
        if (!row || row->pair->source != roster->map_provider)
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply admission omits an actual source player");
    }
    return expected == actual || application_fail(error, QA_ERROR_FORMAT,
        "Saved supply admission contains a player outside the actual source roster");
}
static bool timers_valid(supply_actor *row, qa_error *error) {
    supply_pair *pair = row->pair;
    bool mapped = pair->source->kind == APPLICATION_PROVIDER_Q3 && !pair->native;
    if (row->timers_ready != mapped || row->timer_count != (mapped ? pair->profile.ammo_owner_count : 0))
        return application_fail(error, QA_ERROR_FORMAT, "Saved mapped ammo timer inventory differs from its actual source");
    for (size_t i = 0; i < row->timer_count; ++i) {
        supply_timer *timer = row->timers + i;
        const qa_supply_source_owner *binding = NULL;
        for (size_t j = 0; j < pair->profile.ammo_owner_count; ++j)
            if (pair->profile.ammo_owners[j].item == timer->item &&
                pair->profile.ammo_owners[j].source == timer->source) {
                binding = pair->profile.ammo_owners + j; break;
            }
        if (!binding) return application_fail(error, QA_ERROR_FORMAT,
            "Saved mapped ammo pool differs from its authored supply profile");
        timer->weapon = timer_weapon(pair, timer->source);
        if (timer->weapon == QA_Q3_W_NONE || timer->elapsed < 0 || timer->elapsed >= timer_period[timer->weapon])
            return application_fail(error, QA_ERROR_FORMAT, "Invalid original mapped ammo regeneration counter");
        for (size_t j = 0; j < i; ++j)
            if (row->timers[j].item == timer->item)
                return application_fail(error, QA_ERROR_FORMAT, "Duplicate saved mapped ammo pool");
    }
    /* Saved rows may arrive in any source-valid order; the actual timer
     * producer still visits destination pools in authored binding order. */
    for (size_t i = 0; i < row->timer_count; ++i) {
        const qa_supply_source_owner *binding = pair->profile.ammo_owners + i;
        size_t found = i;
        while (found < row->timer_count && (row->timers[found].item != binding->item ||
            row->timers[found].source != binding->source)) ++found;
        if (found == row->timer_count)
            return application_fail(error, QA_ERROR_FORMAT, "Saved mapped ammo pool is missing");
        supply_timer prior = row->timers[i];
        row->timers[i] = row->timers[found]; row->timers[found] = prior;
    }
    return true;
}
static bool supply_signature(qa_source_save_io *io, qa_error *error) {
    static const uint8_t expected[4] = {'Q','A','S','P'};
    uint8_t magic[sizeof(expected)]; memcpy(magic, expected, sizeof(magic));
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        (!memcmp(magic, expected, sizeof(magic)) ||
         application_fail(error, QA_ERROR_FORMAT, "Unsupported application supplies continuation schema"));
}
static bool supply_pairs_field(application_supplies *owner, qa_source_save_io *io, qa_error *error) {
    size_t expected = pair_count(owner), count = expected;
    if (!qa_source_save_count(io, &count, expected) || count != expected)
        return application_fail(error, QA_ERROR_FORMAT, "Saved supplies omit an actual prepared source pair");
    for (supply_pair *pair = owner->pairs; pair; pair = pair->next) {
        const char *source = pair->source->launch->selection.instance;
        const char *arsenal = pair->arsenal->launch->selection.instance;
        const char *source_name = source, *arsenal_name = arsenal;
        qa_string_id profile = pair->profile_id;
        bool native = pair->native;
        if (!qa_source_save_text(io, &source_name) || !qa_source_save_text(io, &arsenal_name) ||
            !qa_source_save_string(io, &profile) || !qa_source_save_bool(io, &native)) return false;
        if (!source_name || !arsenal_name || strcmp(source_name, source) || strcmp(arsenal_name, arsenal) ||
            profile != pair->profile_id || native != pair->native)
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply pair differs from its actual selected source owners");
    }
    return true;
}
static bool supply_actors_field(application_supplies *owner, qa_source_save_io *io,
    supply_actor **head, qa_error *error) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (const supply_actor *row = *head; row; row = row->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(supply_actor))) return false;
    if (reading && count > (io->input.size - io->offset) / 30)
        return application_fail(error, QA_ERROR_FORMAT, "Truncated saved supply actor inventory");
    supply_actor **link = head;
    size_t pairs = pair_count(owner);
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *link = calloc(1, sizeof(**link));
            if (!*link) return application_fail(error, QA_ERROR_MEMORY, "Decoding actual supply actor admissions");
        }
        supply_actor *row = *link;
        size_t ordinal = 0;
        if (!reading) {
            supply_pair *pair = owner->pairs;
            while (pair && pair != row->pair) { pair = pair->next; ++ordinal; }
        }
        if (!pairs || !qa_source_save_count(io, &ordinal, pairs - 1))
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply actor has no prepared source pair");
        if (reading) row->pair = pair_ordinal(owner, ordinal);
        if (!qa_source_save_actor(io, &row->actor)) return false;
        if (!row->actor.registry)
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply admission has no actual actor");
        if (!qa_source_save_bool(io, &row->timers_ready) ||
            !qa_source_save_count(io, &row->timer_count, row->pair->profile.ammo_owner_count)) return false;
        if (reading && row->timer_count) {
            if (row->timer_count > (io->input.size - io->offset) / 12)
                return application_fail(error, QA_ERROR_FORMAT, "Truncated saved mapped ammo pools");
            row->timers = calloc(row->timer_count, sizeof(*row->timers));
            if (!row->timers) return application_fail(error, QA_ERROR_MEMORY, "Decoding actual mapped ammo counters");
        }
        for (size_t j = 0; j < row->timer_count; ++j) {
            supply_timer *timer = row->timers + j;
            if (!qa_source_save_string(io, &timer->item) || !qa_source_save_string(io, &timer->source) ||
                !qa_source_save_i32(io, &timer->elapsed)) return false;
        }
        if (!timers_valid(row, error)) return false;
        link = &row->next;
    }
    return true;
}
static bool supply_codec_ready(qa_application *app, qa_error *error) {
    if (!app || app->operation != APPLICATION_PERSISTING || app->client_preparation || !app->supplies ||
        app->supplies->application != app || app->supplies->inventory != app->inventory ||
        !application_supplies_idle(app->supplies))
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply continuation requires its idle actual publication owner");
    return true;
}
static bool capture_current(application_supplies *owner, const supply_actor *saved,
    qa_error *error) {
    if (!roster_matches(owner, saved, error)) return false;
    for (const supply_actor *row = saved; row; row = row->next) {
        const supply_actor *actual = actor_find(owner, row->pair, row->actor);
        if (!actual || actual->timers_ready != row->timers_ready ||
            actual->timer_count != row->timer_count)
            return application_fail(error, QA_ERROR_ARGUMENT, "Supply admission changed during capture");
        for (size_t i = 0; i < row->timer_count; ++i) {
            const supply_timer *a = actual->timers + i, *b = row->timers + i;
            if (a->item != b->item || a->source != b->source || a->elapsed != b->elapsed)
                return application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo counter changed during capture");
        }
    }
    return true;
}
bool qa_application_supplies_capture(qa_application *app, qa_buffer *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Supply capture requires an owned output");
    if (!supply_codec_ready(app, error)) return false;
    application_supplies *owner = app->supplies;
    supply_actor *copy = NULL, **tail = &copy;
    for (const supply_actor *row = owner->actors; row; row = row->next) {
        *tail = calloc(1, sizeof(**tail));
        if (!*tail) { actor_list_free(copy); return application_fail(error, QA_ERROR_MEMORY, "Capturing actual supply admissions"); }
        (*tail)->pair = row->pair; (*tail)->actor = row->actor; tail = &(*tail)->next;
    }
    ++owner->calls;
    bool ok = roster_matches(owner, copy, error);
    for (supply_actor *row = copy; ok && row; row = row->next) {
        supply_actor *actual = actor_find(owner, row->pair, row->actor);
        if (row->pair->source->kind == APPLICATION_PROVIDER_Q3 && !row->pair->native)
            ok = timers_require(row->pair, row->actor, &actual, error);
        if (ok && !actual) ok = application_fail(error, QA_ERROR_ARGUMENT, "Supply actor retired during capture");
        if (ok) {
            row->timers_ready = actual->timers_ready; row->timer_count = actual->timer_count;
            if (row->timer_count) {
                row->timers = malloc(row->timer_count * sizeof(*row->timers));
                if (!row->timers) { ok = application_fail(error, QA_ERROR_MEMORY, "Capturing mapped ammo counters"); break; }
                memcpy(row->timers, actual->timers, row->timer_count * sizeof(*row->timers));
            }
        }
    }
    qa_source_save_io io = {0};
    qa_buffer result = {0};
    if (ok) ok = qa_source_save_writer(&io, app->session, error) && supply_signature(&io, error) &&
        supply_pairs_field(owner, &io, error) && supply_actors_field(owner, &io, &copy, error) &&
        capture_current(owner, copy, error) && qa_source_save_finish(&io, &result);
    qa_source_save_dispose(&io); actor_list_free(copy); --owner->calls;
    if (ok) *out = result;
    else qa_buffer_free(&result);
    return ok;
}
bool qa_application_supplies_restore(qa_application *app, qa_bytes bytes, qa_error *error) {
    if (!supply_codec_ready(app, error)) return false;
    application_supplies *owner = app->supplies;
    ++owner->calls;
    qa_source_save_io io = {0};
    supply_actor *decoded = NULL;
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && supply_signature(&io, error) &&
        supply_pairs_field(owner, &io, error) && supply_actors_field(owner, &io, &decoded, error) &&
        qa_source_save_finish(&io, NULL) && roster_matches(owner, decoded, error);
    qa_source_save_dispose(&io);
    /* Complete source identity/actor/pool decoding precedes any inventory
     * admission callback. Failure retains the ordinary candidate's owners. */
    for (supply_actor *row = decoded; ok && row; row = row->next) {
        if (!actor_find(owner, row->pair, row->actor))
            ok = application_supplies_admit(owner, row->pair->source, row->actor, error);
        for (size_t i = 0; ok && i < row->timer_count; ++i) {
            qa_inventory_entry entry;
            ok = qa_inventory_entry_read(owner->inventory, row->actor, row->timers[i].item, &entry, error) &&
                supply_current(row->pair, row->actor, error);
        }
    }
    if (ok) ok = roster_matches(owner, decoded, error);
    if (ok) {
        for (supply_actor *row = decoded; row; row = row->next) {
            supply_actor *actual = actor_find(owner, row->pair, row->actor);
            row->pickups = actual->pickups; row->pickup_imported = actual->pickup_imported;
        }
        actor_list_free(owner->actors);
        owner->actors = decoded; decoded = NULL;
    }
    actor_list_free(decoded);
    --owner->calls;
    return ok;
}
