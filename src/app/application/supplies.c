#include "supplies.h"
#include "control_frame.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q1_supply.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_combat.h"
#include "guest_qc_internal.h"
#include "guest_qc_pickups.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_primary_pickups.h"
#include "guest_q3_private.h"
#include "guest_q3_pickups.h"
#include "guest_q3_catalog.h"
#include "guest_q3_weapons_services.h"
#include "guest_projection_private.h"
#include "guest_q3_save.h"
#include "map_players_private.h"
#include "qa/application_supplies_save.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include "qa/pool.h"
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
    qa_actor_index actors;
    qa_pool actor_pool;
    size_t timer_offset;
    bool native;
} supply_pair;
typedef struct supply_timer {
    qa_item_id item, source;
    int32_t elapsed;
    qa_q3_weapon weapon;
} supply_timer;
typedef struct supply_actor {
    struct supply_actor *next;
    struct supply_actor **previous;
    supply_pair *pair;
    qa_actor_id actor;
    supply_timer *timers;
    size_t timer_count;
    size_t storage_slot;
    bool timers_ready;
    qa_pickup_lease pickups;
    bool pickup_imported;
} supply_actor;
struct application_supplies {
    qa_application *application;
    qa_inventory *inventory;
    supply_pair *pairs;
    supply_actor *actors;
    supply_actor *retired;
    qa_arena storage;
    size_t calls;
    bool admitting;
};

static bool intern(application_supplies *owner, const char *text, qa_item_id *out,
                    qa_error *error) {
    return qa_strings_intern_cstr(qa_session_strings(owner->application->session), text, out, error);
}
static size_t mapping_index(const qa_supply_mapping *rows, size_t count, qa_item_id source) {
    for (size_t i = 0; i < count; ++i) if (rows[i].source == source) return i;
    return SIZE_MAX;
}
static qa_supply_mapping *mapping(qa_supply_mapping *rows, size_t count, qa_item_id source) {
    size_t i = mapping_index(rows, count, source);
    return i == SIZE_MAX ? NULL : rows + i;
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
    qa_campaign_id campaign = pair->arsenal->product->campaign_id;
    if (ok && target == QA_GAME_Q1 && (campaign == QA_CAMPAIGN_HIPNOTIC)) {
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
        if ((campaign == QA_CAMPAIGN_ROGUE)) selected[selected_count++] = 0;
        else if ((campaign == QA_CAMPAIGN_MG3)) selected[selected_count++] = 1;
    } else if (target == QA_GAME_Q2) {
        if (pair->arsenal->product->edition == QA_EDITION_RERELEASE) {
            selected[selected_count++] = 2; selected[selected_count++] = 3;
        } else if ((campaign == QA_CAMPAIGN_XATRIX)) selected[selected_count++] = 2;
        else if ((campaign == QA_CAMPAIGN_ROGUE)) selected[selected_count++] = 3;
    } else if ((campaign == QA_CAMPAIGN_MISSIONPACK)) selected[selected_count++] = 4;
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
static q3g_role *original_q3_arsenal(const application_provider *provider) {
    struct application_q3_guest *engine = provider && provider->kind == APPLICATION_PROVIDER_QVM
        ? provider->state.qvm.engine : NULL;
    q3g_role *role = engine ? engine->game : NULL;
    qa_q3_product product;
    return role && engine->provider == provider && role->primary && role->kind == QA_QVM_GAME &&
        role->ready && !role->retired && !role->source_cleared && role->vm && role->projection &&
        role->weapons && role->weapon_services && application_q3_catalog_bot_product(role->catalog, &product) &&
        product == QA_Q3_ARENA
        ? role : NULL;
}
static supply_pair *pair_find(application_supplies *owner, application_provider *source,
    application_provider *arsenal) {
    for (supply_pair *pair = owner->pairs; pair; pair = pair->next)
        if (pair->source == source && pair->arsenal == arsenal) return pair;
    return NULL;
}
static supply_actor *actor_find(application_supplies *owner, supply_pair *pair, qa_actor_id actor) {
    (void)owner;
    size_t row;
    return qa_actor_index_find(&pair->actors, actor, &row) ? (supply_actor *)(uintptr_t)row : NULL;
}
static void actor_link(application_supplies *owner, supply_actor *entry) {
    entry->next = owner->actors; entry->previous = &owner->actors;
    if (entry->next) entry->next->previous = &entry->next;
    owner->actors = entry;
    qa_actor_index_put(&entry->pair->actors, entry->actor, (size_t)(uintptr_t)entry);
}
static void actor_dispose(supply_actor *entry) {
    application_supplies *owner = entry->pair->owner;
    if (owner->calls) {
        entry->next = owner->retired;
        owner->retired = entry;
    } else qa_pool_release(&entry->pair->actor_pool, entry->storage_slot);
}
static void actor_list_free(supply_actor *head) {
    while (head) {
        supply_actor *next = head->next;
        actor_dispose(head);
        head = next;
    }
}
static void supplies_leave(application_supplies *owner) {
    if (--owner->calls) return;
    while (owner->retired) {
        supply_actor *entry = owner->retired;
        owner->retired = entry->next;
        qa_pool_release(&entry->pair->actor_pool, entry->storage_slot);
    }
}
static bool original_player_current(application_provider *provider, qa_actor_id actor,
    qa_error *error) {
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (engine && engine->provider == provider && engine->initialized && engine->clients &&
            provider->state.qc.instance && !engine->loading) {
            const application_qc_client *client = application_qc_client_for_actor(engine, actor);
            qa_qc_slot_binding binding;
            const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
            if (client && client->connected && record &&
                qa_qc_slot(provider->state.qc.instance, (uint32_t)(client - engine->clients), &binding) &&
                qa_actor_id_equal(binding.actor, actor) && binding.owner == record->owner &&
                binding.source_slot == (record->has_source ? record->source_slot : 0) &&
                (binding.kind == QA_QC_SLOT_BORROWED || binding.kind == QA_QC_SLOT_OWNED)) return true;
        }
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        uint32_t slot = application_native_q2_client_slot(engine, actor);
        if (engine->provider == provider && engine->initialized && !engine->shutting_down && slot) {
            const application_native_q2_client *client = engine->clients + slot;
            if (client->connected && !client->disconnect_started) return true;
        }
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        uint32_t slot;
        if (application_guest_q3_save_actor_client(provider, actor, &slot)) return true;
        if (engine && engine->provider == provider && engine->game && engine->game->primary &&
            engine->game->initialized && !engine->game->retired && engine->game->host &&
            qa_q3_host_actor_slot(engine->game->host, actor, &slot, NULL) &&
            slot < sizeof(engine->clients)/sizeof(*engine->clients)) {
            const q3g_client *client = engine->clients + slot;
            if (client->allocated && client->connected && !client->disconnect_pending &&
                !client->disconnect_started && !client->pending_retirement &&
                qa_actor_id_equal(client->actor, actor)) return true;
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
    const application_player_record *record=application_player_record_find(roster,actor);
    if (!record || record->retiring) return application_fail(error, QA_ERROR_ARGUMENT, "Supply source actor has no actual roster admission");
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_source_client_view client;
        if (qa_q1_source_client_read(source->state.q1, actor, &client) &&
            client.slot == record->client_slot) return true;
    } else if (source->kind == APPLICATION_PROVIDER_Q2) {
        qa_builtin_player_info client;
        if (qa_q2_player_projection(source->state.q2, actor, NULL, &client) &&
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
    if (pair->source == pair->arsenal) return true;
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
    } else if (original_q3_arsenal(pair->arsenal))
        return original_player_current(pair->arsenal, actor, error);
    return application_fail(error, QA_ERROR_ARGUMENT, "Selected supply has no actual native arsenal actor");
}
static bool supply_current(void *opaque, qa_actor_id actor, qa_error *error) {
    supply_pair *pair = opaque;
    return pair_current(pair, actor, error) && (actor_find(pair->owner, pair, actor) != NULL ||
        application_fail(error, QA_ERROR_ARGUMENT, "Selected supply actor was not admitted"));
}
typedef struct q3_supply_arsenal {
    const application_q3_catalog_weapon *weapons;
    size_t count;
    application_q3_catalog_weapon native[QA_Q3_WEAPON_COUNT - 1];
} q3_supply_arsenal;
static bool q3_arsenal_read(supply_pair *pair, q3_supply_arsenal *out, qa_error *error) {
    *out = (q3_supply_arsenal){0};
    application_provider *arsenal = pair->arsenal;
    if (arsenal->kind == APPLICATION_PROVIDER_Q3) {
        int limit = arsenal->product->campaign_id == QA_CAMPAIGN_MISSIONPACK
            ? QA_Q3_WEAPON_COUNT : QA_Q3_W_GRAPPLE + 1;
        out->weapons = out->native;
        for (int weapon = QA_Q3_W_GAUNTLET; weapon < limit; ++weapon)
            out->native[out->count++] = (application_q3_catalog_weapon){.weapon = weapon,
                .item = qa_q3_weapon_item(arsenal->state.q3, (qa_q3_weapon)weapon, false),
                .ammo = qa_q3_weapon_item(arsenal->state.q3, (qa_q3_weapon)weapon, true)};
        return true;
    }
    q3g_role *role = original_q3_arsenal(arsenal);
    return role ? application_q3_catalog_weapons(role->catalog, &out->weapons, &out->count, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Selected Q3 supply has no admitted original catalog");
}
static const application_q3_catalog_weapon *q3_arsenal_weapon(const q3_supply_arsenal *arsenal,
    int32_t weapon) {
    for (size_t i = 0; i < arsenal->count; ++i)
        if (arsenal->weapons[i].weapon == weapon) return arsenal->weapons + i;
    return NULL;
}
static bool q3_active_weapon(supply_pair *pair, qa_actor_id actor, int32_t *out, qa_error *error) {
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(pair->arsenal->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 pickup receipt lost its arsenal actor");
        *out = player.weapon;
        return true;
    }
    q3g_role *role = original_q3_arsenal(pair->arsenal);
    application_q3_weapon_prediction prediction;
    if (!role) return application_fail(error, QA_ERROR_NOT_FOUND, "Original Q3 supply lost its actual weapon owner");
    if (!application_q3_weapons_prediction_read(role->weapons, actor, &prediction, error)) return false;
    *out = prediction.source_weapon;
    return true;
}
static bool q3_request_weapon(supply_pair *pair, qa_actor_id actor,
    const application_q3_catalog_weapon *weapon, qa_error *error) {
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_player_request_weapon(pair->arsenal->state.q3, actor,
            (qa_q3_weapon)weapon->weapon, error) && supply_current(pair, actor, error);
    q3g_role *role = original_q3_arsenal(pair->arsenal);
    bool admitted = false;
    if (!role) return application_fail(error, QA_ERROR_NOT_FOUND, "Original Q3 supply lost its actual weapon owner");
    return application_q3_weapons_services_select_intent(role->weapon_services, actor,
        pair->arsenal->owner, weapon->item, &admitted, error) && supply_current(pair, actor, error) &&
        (admitted || application_fail(error, QA_ERROR_NOT_FOUND, "Selected original supply weapon was not admitted"));
}
static application_q3_pickups *original_q3_pickups(const supply_pair *pair) {
    struct application_q3_guest *engine = q3g_engine(pair->source);
    return !pair->native && engine && engine->provider == pair->source && engine->game &&
        engine->game->primary && engine->game->kind == QA_QVM_GAME && engine->game->vm
        ? engine->game->pickups : NULL;
}
static struct application_native_q2_primary_pickups *original_native_pickups(const supply_pair *pair) {
    struct application_native_q2 *engine = pair->source->kind == APPLICATION_PROVIDER_NATIVE
        ? pair->source->state.native.q2_engine : NULL;
    return !pair->native && engine ? engine->primary_pickups : NULL;
}
static struct application_qc_state *original_qc_pickups(const supply_pair *pair) {
    return !pair->native && pair->source->kind == APPLICATION_PROVIDER_QC &&
        application_qc_pickups_source_present(pair->source) ? pair->source->state.qc.engine : NULL;
}
static bool original_take(void *opaque, const qa_pickup_offer *offer,
    qa_pickup_execution *execution, qa_pickup_outcome *out, qa_error *error) {
    supply_pair *pair = opaque;
    *out = QA_PICKUP_REFUSED;
    if (!qa_pickup_current(execution)) return true;
    application_supplies *owner = pair->owner;
    if (owner->calls == SIZE_MAX || offer->source != pair->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original pickup lost its actual selected supply owner");
    ++owner->calls;
    application_q3_pickups *source = original_q3_pickups(pair);
    struct application_native_q2_primary_pickups *native = original_native_pickups(pair);
    struct application_qc_state *qc = original_qc_pickups(pair);
    qa_supply_offer supplied = {0};
    qa_supply_options options = {0};
    qa_unified_frame_lease *storage = NULL;
    bool accepted = false;
    bool ok = supply_current(pair, offer->recipient, error) && (qc ?
        application_qc_pickups_supply(qc, offer, &supplied, &options, error) : native ?
        application_native_q2_primary_pickups_supply(native, offer, &supplied, &options, error) :
        application_q3_pickups_supply(source, offer, &supplied, &options, error));
    if (ok && supplied.kind != QA_SUPPLY_AMMO && supplied.kind != QA_SUPPLY_WEAPON &&
        supplied.kind != QA_SUPPLY_AMMO_WEAPON)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Original pickup has no static supply grant");
    if (ok && offer->cargo_count == 1) {
        const qa_pickup_cargo *cargo = offer->cargo;
        qa_item_id ammo = cargo->item;
        if (cargo->weapon) {
            qa_item_definition weapon;
            ok = qa_inventory_source_definition_read(owner->application->inventory, offer->recipient,
                pair->arsenal->owner, cargo->item, &weapon, error);
            if (ok) ammo = weapon.ammo;
        }
        if (ok) {
            options.canonical = true;
            supplied.item = supplied.kind == QA_SUPPLY_AMMO_WEAPON && cargo->weapon && ammo ? ammo : cargo->item;
            supplied.weapon = cargo->weapon ? cargo->item : 0;
            if (!cargo->weapon) supplied.kind = QA_SUPPLY_AMMO;
            if (cargo->weapon && supplied.kind == QA_SUPPLY_WEAPON && !ammo) supplied.ammo_count = 0;
            if (supplied.ammo_count) {
                storage = application_control_storage_acquire(owner->application, error);
                qa_pickup_grant *grants = storage ? qa_unified_frame_lease_alloc(storage, supplied.ammo_count,
                    sizeof(*grants), _Alignof(qa_pickup_grant), error) : NULL;
                ok = grants != NULL;
                for (size_t i = 0; ok && i < supplied.ammo_count; ++i) {
                    grants[i] = supplied.ammo[i]; grants[i].item = ammo ? ammo : cargo->item;
                }
                supplied.ammo = grants;
            }
        }
    }
    if (ok) ok = qa_supply_apply(pair->supply, offer->recipient, &supplied, &options,
        &accepted, error) && supply_current(pair, offer->recipient, error);
    if (ok) *out = qa_pickup_current(execution)
        ? (accepted ? QA_PICKUP_ACCEPTED : QA_PICKUP_REFUSED) : QA_PICKUP_STALE;
    qa_unified_frame_lease_release(storage);
    supplies_leave(owner);
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
    struct application_qc_state *qc = original_qc_pickups(pair);
    if (!original_q3_pickups(pair) && !original_native_pickups(pair) && !qc) return true;
    const qa_supply_profile *profile = &pair->profile;
    if (profile->ammo_count > SIZE_MAX - profile->weapon_count)
        return application_fail(error, QA_ERROR_MEMORY, "Original pickup rule inventory overflows");
    size_t count = profile->ammo_count + profile->weapon_count;
    qa_item_id cargo = qc ? application_qc_pickups_cargo_item(pair->source) : 0;
    if (cargo && count == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Original pickup rule inventory overflows");
    if (cargo) ++count;
    if (!count || count > SIZE_MAX / sizeof(*pair->pickup_rules))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected original pickups need their actual supply mappings");
    pair->pickup_rules = calloc(count, sizeof(*pair->pickup_rules));
    if (!pair->pickup_rules) return application_fail(error, QA_ERROR_MEMORY, "Owning selected original pickup rules");
    for (size_t i = 0; i < count; ++i) {
        bool backpack = cargo && i == count - 1;
        bool ammo = i < profile->ammo_count;
        const qa_supply_mapping *mapping = backpack ? NULL : ammo ? profile->ammo + i : profile->weapons + i - profile->ammo_count;
        qa_item_id source_item = backpack ? cargo : mapping->source;
        qa_pickup_rule *rule = NULL;
        for (size_t j = 0; j < pair->pickup_rule_count; ++j)
            if (pair->pickup_rules[j].offered[0] == source_item) { rule = pair->pickup_rules + j; break; }
        if (!rule) {
            rule = pair->pickup_rules + pair->pickup_rule_count++;
            qa_item_id *offered = malloc(sizeof(*offered));
            if (!offered) return application_fail(error, QA_ERROR_MEMORY, "Retaining original pickup item identity");
            *offered = source_item; rule->offered = offered; rule->offered_count = 1;
            qa_bytes item = qa_strings_text(qa_session_strings(pair->owner->application->session), source_item);
            if (!item.data || item.size > SIZE_MAX - 10)
                return application_fail(error, QA_ERROR_ARGUMENT, "Original pickup mapping has no retained item name");
            char *name = malloc(item.size + 10);
            if (!name) return application_fail(error, QA_ERROR_MEMORY, "Naming selected original pickup rule");
            memcpy(name, "selected:", 9); memcpy(name + 9, item.data, item.size); name[item.size + 9] = 0;
            bool named = intern(pair->owner, name, &rule->id, error); free(name);
            if (!named) return false;
            rule->context = pair; rule->take = original_take;
        }
        qa_pickup_write *writes = (qa_pickup_write *)rule->writes;
        bool ok = true;
        for (size_t j = 0; ok && mapping && j < mapping->count; ++j)
            ok = pickup_write_add(&writes, &rule->write_count, mapping->destinations[j], error);
        for (size_t j = 0; ok && backpack && j < profile->weapon_count; ++j)
            for (size_t k = 0; ok && k < profile->weapons[j].count; ++k)
                ok = pickup_write_add(&writes, &rule->write_count, profile->weapons[j].destinations[k], error);
        for (size_t j = 0; ok && !ammo && j < profile->ammo_count; ++j)
            for (size_t k = 0; ok && k < profile->ammo[j].count; ++k)
                ok = pickup_write_add(&writes, &rule->write_count, profile->ammo[j].destinations[k], error);
        rule->writes = writes;
        if (!ok) return false;
        if (!rule->write_count)
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected original pickup has no destination");
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
static bool actor_storage_prepare(supply_pair *pair, qa_error *error) {
    size_t alignment = _Alignof(supply_timer);
    pair->timer_offset = (sizeof(supply_actor) + alignment - 1) & ~(alignment - 1);
    if (pair->profile.ammo_owner_count >
        (SIZE_MAX - pair->timer_offset) / sizeof(supply_timer))
        return application_fail(error, QA_ERROR_MEMORY, "Supply actor payload exceeds native extent");
    size_t stride = pair->timer_offset + pair->profile.ammo_owner_count * sizeof(supply_timer);
    if (alignment < _Alignof(supply_actor)) alignment = _Alignof(supply_actor);
    return qa_pool_prepare(&pair->actor_pool, &pair->owner->storage,
        qa_actors_capacity(qa_session_actors(pair->owner->application->session)),
        stride, alignment, error);
}
static supply_actor *actor_take(supply_pair *pair, qa_error *error) {
    if (pair->actor_pool.head == SIZE_MAX &&
        !qa_pool_extend(&pair->actor_pool, &pair->owner->storage, 1, error)) return NULL;
    size_t slot;
    supply_actor *entry = qa_pool_take(&pair->actor_pool, &slot);
    if (!entry) {
        application_fail(error, QA_ERROR_MEMORY, "Retaining supply actor admission");
        return NULL;
    }
    memset(entry, 0, pair->actor_pool.stride);
    entry->pair = pair;
    entry->storage_slot = slot;
    if (pair->profile.ammo_owner_count)
        entry->timers = (supply_timer *)((uint8_t *)entry + pair->timer_offset);
    return entry;
}

bool application_supplies_create(qa_application *app, qa_inventory *inventory,
    application_supplies **out, qa_error *error) {
    if (!app || !app->session || !inventory || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Supplies require actual application inventory");
    *out = NULL;
    application_supplies *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating application supplies");
    qa_arena_init(&owner->storage, 0);
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
        if (owner->actors) owner->actors->previous = &owner->actors;
        qa_actor_index_remove(&entry->pair->actors, entry->actor);
        actor_dispose(entry);
    }
    while (owner->pairs) {
        supply_pair *pair = owner->pairs; owner->pairs = pair->next;
        qa_actor_index_destroy(&pair->actors);
        qa_supply_destroy(pair->supply); pickup_rules_free(pair); profile_free(&pair->profile); free(pair);
    }
    qa_arena_destroy(&owner->storage);
    free(owner); return true;
}
bool application_supplies_prepare(application_supplies *owner, application_provider *source,
    application_provider *arsenal, qa_error *error) {
    if (!owner || !application_supplies_idle(owner) || !source || !arsenal ||
        source->application != owner->application || arsenal->application != owner->application ||
        !source->constructed || !arsenal->constructed || source->close_pending || arsenal->close_pending ||
        !source->product || !arsenal->product ||
        (source != arsenal && arsenal->kind != APPLICATION_PROVIDER_Q1 && arsenal->kind != APPLICATION_PROVIDER_Q2 &&
         arsenal->kind != APPLICATION_PROVIDER_Q3 && !original_q3_arsenal(arsenal)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Supply preparation needs actual source and qualified arsenal owners");
    if (pair_find(owner, source, arsenal)) return true;
    supply_pair *pair = calloc(1, sizeof(*pair));
    if (!pair) return application_fail(error, QA_ERROR_MEMORY, "Allocating selected supply pair");
    if (!qa_actor_index_create(&pair->actors, qa_session_actors(owner->application->session), NULL, error)) {
        free(pair); return false;
    }
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
    if (ok) ok = actor_storage_prepare(pair, error);
    supplies_leave(owner);
    if (!ok) { qa_actor_index_destroy(&pair->actors); qa_supply_destroy(pair->supply); pickup_rules_free(pair); profile_free(&pair->profile); free(pair); return false; }
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
    qa_unified_frame_lease *storage, qa_inventory_entry **out, size_t *count, qa_error *error) {
    *out = NULL; *count = 0;
    qa_inventory_entry *current = NULL;
    size_t current_count = 0;
    if (!qa_inventory_entries(pair->owner->inventory, actor, NULL, 0, &current_count, error) ||
        !pair_current(pair, actor, error)) return false;
    if (current_count > SIZE_MAX / sizeof(*current))
        return application_fail(error, QA_ERROR_MEMORY, "Supply inventory exceeds native extent");
    if (current_count) {
        current = qa_unified_frame_lease_alloc(storage, current_count,
            sizeof(*current), _Alignof(qa_inventory_entry), error);
        if (!current) return false;
        size_t read_count = 0;
        if (!qa_inventory_entries(pair->owner->inventory, actor, current, current_count, &read_count, error) ||
            !pair_current(pair, actor, error)) return false;
        current_count = read_count;
    }
    const qa_supply_mapping *lists[] = {pair->profile.weapons, pair->profile.ammo};
    size_t counts[] = {pair->profile.weapon_count, pair->profile.ammo_count};
    size_t capacity = 0;
    for (size_t list = 0; list < 2; ++list)
        for (size_t i = 0; i < counts[list]; ++i) {
            if (lists[list][i].count > SIZE_MAX - capacity)
                return application_fail(error, QA_ERROR_MEMORY, "Supply admission exceeds native extent");
            capacity += lists[list][i].count;
        }
    if (capacity) {
        *out = qa_unified_frame_lease_alloc(storage, capacity,
            sizeof(**out), _Alignof(qa_inventory_entry), error);
        if (!*out) return false;
    }
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
                (*out)[(*count)++] = entry;
            }
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
        owner->admitting = false; supplies_leave(owner);
        return ok;
    }
    supply_actor *entry = actor_take(pair, error);
    if (!entry) return false;
    entry->actor = actor;
    ++owner->calls;
    owner->admitting = true;
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    qa_inventory_admission *admission = NULL;
    qa_unified_frame_lease *storage = pair->native ? NULL :
        application_control_storage_acquire(owner->application, error);
    bool ok = pair->native || storage != NULL;
    if (ok && !pair->native) {
        if (arsenal->kind == APPLICATION_PROVIDER_Q2)
            ok = qa_q2_items_admit_player(arsenal->state.q2, actor, false, error);
        else if (arsenal->kind == APPLICATION_PROVIDER_Q3)
            ok = qa_q3_inventory_admit(arsenal->state.q3, actor, error);
        else if (original_q3_arsenal(arsenal))
            ok = application_guest_actor_admit(arsenal, actor, error);
        if (ok) ok = pair_current(pair, actor, error);
    }
    if (ok) ok = pair->native || (admission_entries(pair, actor, storage, &entries, &count, error) &&
        qa_inventory_prepare_entries(owner->inventory, actor, entries, count, &admission, error) &&
        pair_current(pair, actor, error) && qa_inventory_admission_validate(admission, error) &&
        pair_current(pair, actor, error) && qa_inventory_admission_commit(admission, error));
    if (ok) admission = NULL;
    qa_inventory_admission_abort(admission);
    qa_unified_frame_lease_release(storage);
    if (ok) { actor_link(owner, entry);
        ok = pickup_actor_bind(entry, error); }
    else actor_dispose(entry);
    owner->admitting = false;
    supplies_leave(owner); return ok;
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
    if (!pair || !arsenal || arsenal->owner != provider ||
        (!original_q3_pickups(pair) && !original_native_pickups(pair)) ||
        !pair_current(pair, actor, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved pickup rule differs from its actual Source and arsenal actor");
    const qa_pickup_rule *rule = NULL;
    for (size_t i = 0; i < pair->pickup_rule_count; ++i)
        if (pair->pickup_rules[i].id == id) { rule = pair->pickup_rules + i; break; }
    if (!rule) return application_fail(error, QA_ERROR_FORMAT, "Saved selected pickup has no authored mapping rule");
    supply_actor *entry = actor_find(owner, pair, actor);
    if (!entry) {
        entry = actor_take(pair, error);
        if (!entry) return false;
        entry->actor = actor; actor_link(owner, entry);
    }
    if (entry->pickup_imported && entry->pickups.serial != serial)
        return application_fail(error, QA_ERROR_FORMAT, "Saved selected pickup changed its actual registration identity");
    entry->pickups = (qa_pickup_lease){actor, serial}; entry->pickup_imported = true;
    *out = *rule;
    return true;
}
void application_supplies_actor_released(application_supplies *owner, qa_actor_record actor) {
    if (!owner) return;
    for (supply_pair *pair = owner->pairs; pair; pair = pair->next) {
        supply_actor *entry = actor_find(owner, pair, actor.id);
        if (!entry) continue;
        qa_actor_index_remove(&pair->actors, actor.id);
        *entry->previous = entry->next;
        if (entry->next) entry->next->previous = entry->previous;
        actor_dispose(entry);
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
bool application_supplies_ammo_source(application_supplies *owner,
    application_provider *source, qa_actor_id actor, qa_item_id item,
    const qa_item_id *available, size_t available_count, qa_item_id *out, qa_error *error)
{
    if (!owner || !source || !item || !out || (available_count && !available))
        return application_fail(error, QA_ERROR_ARGUMENT, "Ammo source requires its actual supply association and source roster");
    *out = 0;
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair || !supply_current(pair, actor, error))
        return pair ? false : application_fail(error, QA_ERROR_ARGUMENT, "Ammo source lost its admitted supply pair");
    qa_item_id preferred = 0, candidate = 0;
    size_t matches = 0;
    for (size_t i = 0; i < pair->profile.ammo_owner_count; ++i)
        if (pair->profile.ammo_owners[i].item == item) {
            preferred = pair->profile.ammo_owners[i].source; break;
        }
    for (size_t i = 0; i < pair->profile.ammo_count; ++i) {
        const qa_supply_mapping *row = pair->profile.ammo + i;
        bool contains = false;
        for (size_t j = 0; j < row->count; ++j)
            if (row->destinations[j] == item) { contains = true; break; }
        if (!contains) continue;
        if (row->source == item) for (size_t j = 0; j < available_count; ++j)
            if (available[j] == item) { *out = item; return true; }
        if (!preferred || row->source == preferred) { candidate = row->source; ++matches; }
    }
    if (matches == 1) for (size_t i = 0; i < available_count; ++i)
        if (available[i] == candidate) { *out = candidate; break; }
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
bool application_supplies_weapon_droppable(const application_provider *provider, qa_item_id item)
{
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return item != qa_q3_weapon_item(provider->state.q3, QA_Q3_W_GAUNTLET, false) &&
            item != qa_q3_weapon_item(provider->state.q3, QA_Q3_W_GRAPPLE, false);
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return item != qa_q1_weapon_item(provider->state.q1, QA_Q1_ROGUE_GRAPPLE) &&
            item != qa_q1_weapon_item(provider->state.q1, QA_Q1_CTF_GRAPPLE);
    q3g_role *role = original_q3_arsenal(provider);
    if (role) {
        const application_q3_catalog_weapon *weapons; size_t count;
        if (!application_q3_catalog_weapons(role->catalog, &weapons, &count, NULL)) return false;
        for (size_t i = 0; i < count; ++i)
            if (weapons[i].item == item)
                return weapons[i].weapon != QA_Q3_W_GAUNTLET && weapons[i].weapon != QA_Q3_W_GRAPPLE;
    }
    return true;
}

bool application_supplies_weapon_sources(application_supplies *owner, application_provider *source,
    qa_actor_id actor, const qa_supply_weapon *selected, size_t selected_count,
    const qa_supply_weapon *original, size_t original_count, qa_arena *storage,
    qa_item_id *out, qa_error *error) {
    if (!owner || !source || owner->calls == SIZE_MAX || (selected_count && (!selected || !out)) ||
        (original_count && !original))
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon source mapping needs its actual supply owner and rosters");
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_NOT_FOUND, "Weapon source mapping lost its prepared supply pair");
    ++owner->calls;
    bool ok = supply_current(pair, actor, error) &&
        qa_supply_weapon_sources(&pair->profile, selected, selected_count,
            original, original_count, storage, out, error) && supply_current(pair, actor, error);
    supplies_leave(owner); return ok;
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
        if (!qa_q2_player_projection(provider->state.q2, actor, NULL, &player) &&
            !qa_q2_weapon_read(provider->state.q2, actor, &weapon, error)) return false;
    } else if (!original_player_current(provider, actor, error)) return false;
    *out = NULL;
    return true;
}

static const qa_q2_weapon q2_pickup_order[] = {
    QA_Q2_BLASTER, QA_Q2_CHAINFIST, QA_Q2_SHOTGUN, QA_Q2_SUPERSHOTGUN,
    QA_Q2_MACHINEGUN, QA_Q2_ETF_RIFLE, QA_Q2_CHAINGUN, QA_Q2_GRENADES,
    QA_Q2_TRAP, QA_Q2_TESLA, QA_Q2_GRENADELAUNCHER, QA_Q2_PROXLAUNCHER,
    QA_Q2_ROCKETLAUNCHER, QA_Q2_HYPERBLASTER, QA_Q2_IONRIPPER,
    QA_Q2_HEATBEAM, QA_Q2_RAILGUN, QA_Q2_PHALANX,
    QA_Q2_DISINTEGRATOR, QA_Q2_BFG
};
static int q2_rank(supply_pair *pair, qa_item_id item) {
    if (!item) return -1;
    for (size_t i = 0; i < sizeof(q2_pickup_order)/sizeof(*q2_pickup_order); ++i)
        if (qa_q2_weapon_item(pair->arsenal->state.q2, q2_pickup_order[i], false) == item) return (int)i;
    return -1;
}
static bool arsenal_item(supply_pair *pair, qa_item_id item, bool *out, qa_error *error) {
    application_provider *arsenal = pair->arsenal;
    *out = false;
    if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        for (int weapon = QA_Q1_AXE; weapon < QA_Q1_WEAPON_COUNT; ++weapon)
            if (qa_q1_weapon_item(arsenal->state.q1, (qa_q1_weapon)weapon) == item) { *out = true; return true; }
        for (int ammo = QA_Q1_SHELLS; ammo < QA_Q1_AMMO_COUNT; ++ammo)
            if (qa_q1_ammo_item(arsenal->state.q1, (qa_q1_ammo)ammo) == item) { *out = true; return true; }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        for (int weapon = QA_Q2_BLASTER; weapon < QA_Q2_WEAPON_COUNT; ++weapon) {
            if (qa_q2_weapon_item(arsenal->state.q2, (qa_q2_weapon)weapon, false) == item ||
                qa_q2_weapon_item(arsenal->state.q2, (qa_q2_weapon)weapon, true) == item) { *out = true; return true; }
        }
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q3 || original_q3_arsenal(arsenal)) {
        q3_supply_arsenal roster;
        if (!q3_arsenal_read(pair, &roster, error)) return false;
        for (size_t i = 0; i < roster.count; ++i)
            if (roster.weapons[i].item == item || roster.weapons[i].ammo == item) { *out = true; break; }
    }
    return true;
}
static bool source_item(const supply_pair *pair, qa_item_id item) {
    const qa_supply_mapping *lists[] = {pair->profile.weapons, pair->profile.ammo};
    size_t counts[] = {pair->profile.weapon_count, pair->profile.ammo_count};
    for (size_t list = 0; list < 2; ++list)
        for (size_t i = 0; i < counts[list]; ++i)
            if (lists[list][i].source == item) return true;
    return false;
}
typedef struct cheat_inventory {
    qa_unified_frame_lease *storage;
    qa_item_definition *definitions;
    qa_inventory_entry *entries;
    size_t definition_count, entry_count;
} cheat_inventory;
static bool cheat_pair(void *context, qa_actor_id actor, supply_pair **out, qa_error *error) {
    application_provider *publisher = context;
    qa_application *app = publisher ? publisher->application : NULL;
    application_supplies *owner = app ? app->supplies : NULL;
    if (!owner || owner->calls == SIZE_MAX || !provider_current(owner, publisher))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected cheat grant lost its actual supplier");
    *out = NULL;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (source && source == arsenal) return true;
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_ARGUMENT, "Selected cheat grant has no admitted supply pair");
    if (!supply_current(pair, actor, error)) return false;
    *out = pair; return true;
}
static bool cheat_inventory_read(supply_pair *pair, qa_actor_id actor,
    cheat_inventory *view, qa_error *error) {
    application_supplies *owner = pair->owner;
    if (!view->storage) view->storage = application_control_storage_acquire(owner->application, error);
    size_t written = 0;
    bool okay = view->storage && qa_inventory_entries(owner->inventory, actor, NULL, 0,
        &view->entry_count, error) && supply_current(pair, actor, error);
    if (okay && (view->entry_count > SIZE_MAX / sizeof(*view->definitions) ||
        view->entry_count > SIZE_MAX / sizeof(*view->entries)))
        okay = application_fail(error, QA_ERROR_MEMORY, "Selected grant exceeds native inventory extent");
    if (okay) {
        view->definitions = view->entry_count ? qa_unified_frame_lease_alloc(view->storage, view->entry_count,
            sizeof(*view->definitions), _Alignof(qa_item_definition), error) : NULL;
        view->entries = view->entry_count ? qa_unified_frame_lease_alloc(view->storage, view->entry_count,
            sizeof(*view->entries), _Alignof(qa_inventory_entry), error) : NULL;
        if (view->entry_count && (!view->definitions || !view->entries))
            okay = application_fail(error, QA_ERROR_MEMORY, "Retaining actual selected arsenal grant");
    }
    if (okay) {
        okay = qa_inventory_entries(owner->inventory, actor, view->entries, view->entry_count,
            &written, error) && supply_current(pair, actor, error);
        if (okay && written > view->entry_count)
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Selected grant inventory changed extent");
        if (okay) view->entry_count = written;
    }
    for (size_t i = 0; okay && i < view->entry_count; ++i) {
        qa_item_definition definition;
        qa_error lookup = {0};
        bool found = qa_inventory_source_definition_read(owner->inventory, actor,
            pair->arsenal->owner, view->entries[i].item, &definition, &lookup);
        okay = supply_current(pair, actor, error);
        if (!okay) break;
        if (!found) {
            if (lookup.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = lookup;
            okay = false;
            break;
        }
        view->definitions[view->definition_count++] = definition;
    }
    return okay;
}
static bool cheat_weapon(const cheat_inventory *view, qa_item_id item) {
    for (size_t i = 0; i < view->definition_count; ++i)
        if (view->definitions[i].weapon && view->definitions[i].item == item) return true;
    return false;
}
static bool cheat_arsenal_item(const cheat_inventory *view, qa_item_id item) {
    for (size_t i = 0; i < view->definition_count; ++i)
        if (view->definitions[i].weapon && (view->definitions[i].item == item || view->definitions[i].ammo == item)) return true;
    return false;
}
bool application_supplies_cheat_arsenal(void *context, qa_actor_id actor,
    qa_q1_cheat_grant category, bool *handled, qa_error *error) {
    if (!handled || (category != QA_Q1_CHEAT_WEAPONS && category != QA_Q1_CHEAT_AMMO))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal grant has no category or result");
    *handled = false;
    supply_pair *pair;
    if (!cheat_pair(context, actor, &pair, error)) return false;
    if (!pair) return true;
    application_supplies *owner = pair->owner;
    ++owner->calls;
    cheat_inventory view = {0};
    bool okay = cheat_inventory_read(pair, actor, &view, error);
    const application_q3_catalog_weapon *original = NULL; size_t original_count = 0;
    if (okay && pair->source->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(pair->source);
        if (!engine || !engine->game)
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "Original cheat grant lost its actual source catalog");
        else okay = application_q3_catalog_weapons(engine->game->catalog, &original, &original_count, error);
    }
    bool declared = false;
    for (size_t i = 0; okay && i < view.entry_count; ++i) {
        bool weapon = false, ammunition = false;
        for (size_t j = 0; j < view.definition_count; ++j) {
            const qa_item_definition *definition = view.definitions + j;
            if (definition->owner != pair->arsenal->owner || !definition->weapon) continue;
            declared = true;
            weapon |= definition->item == view.entries[i].item;
            ammunition |= definition->ammo && definition->ammo == view.entries[i].item;
        }
        if (category == QA_Q1_CHEAT_WEAPONS ? !weapon : weapon || !ammunition) continue;
        qa_inventory_entry entry = view.entries[i];
        bool native = false;
        for (size_t j = 0; j < original_count; ++j)
            if (original[j].item == entry.item || original[j].ammo == entry.item) { native = true; break; }
        if (native) continue;
        entry.count = category == QA_Q1_CHEAT_WEAPONS ? 1 : entry.capacity;
        okay = qa_inventory_configure(owner->inventory, actor, &entry, NULL, NULL, error) &&
            supply_current(pair, actor, error);
    }
    if (okay && !declared)
        okay = application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected cheat grant has no admitted arsenal definitions");
    if (okay) *handled = true;
    qa_unified_frame_lease_release(view.storage);
    supplies_leave(owner);
    return okay;
}
static bool cheat_name_equal(qa_arena *storage, const char *name, const char *requested,
    bool *out, qa_error *error) {
    qa_buffer lower = {0};
    if (!qa_utf8_lower_arena((qa_bytes){(const uint8_t *)name, strlen(name)}, storage, &lower, error)) return false;
    const char *left = (const char *)lower.data, *right = requested;
    while (true) {
        while (*left == ' ' || *left == '_') ++left;
        while (*right == ' ' || *right == '_') ++right;
        if (*left != *right) { *out = false; return true; }
        if (!*left) { *out = true; return true; }
        ++left; ++right;
    }
}
static bool cheat_quantity(const char *text, bool *present, double *out, qa_error *error) {
    *present = false;
    const char *digits = text;
    if (*digits == '-') ++digits;
    if (!*digits) return true;
    for (const char *at = digits; *at; ++at) if (*at < '0' || *at > '9') return true;
    if (!qa_parse_atof(text, out, error)) return false;
    *present = true; return true;
}
static bool cheat_write(supply_pair *pair, qa_actor_id actor, qa_inventory_entry entry,
    double count, qa_error *error) {
    entry.count = fmax(0, count);
    if (!original_q3_arsenal(pair->arsenal)) entry.capacity = fmax(entry.capacity, entry.count);
    return qa_inventory_configure(pair->owner->inventory, actor, &entry, NULL, NULL, error) &&
        supply_current(pair, actor, error);
}
static bool cheat_request(qa_unified_frame_lease *storage, size_t argc,
    const char *const *argv, char **out, qa_error *error) {
    size_t bytes = 1;
    for (size_t i = 0; i < argc; ++i) {
        size_t length = strlen(argv[i]);
        size_t separator = i != 0;
        if (bytes > SIZE_MAX - separator || length > SIZE_MAX - bytes - separator)
            return application_fail(error, QA_ERROR_MEMORY, "Selected grant name exceeds native extent");
        bytes += length + separator;
    }
    char *joined = qa_unified_frame_lease_alloc(storage, bytes, 1, 1, error);
    if (!joined || !qa_text_join_words(argv, argc, joined, bytes, error)) return false;
    qa_buffer lower = {0};
    if (!qa_utf8_lower_arena((qa_bytes){(const uint8_t *)joined, strlen(joined)},
        qa_unified_frame_lease_arena(storage), &lower, error)) return false;
    *out = (char *)lower.data; return true;
}
static bool cheat_missing(supply_pair *pair, qa_actor_id actor, qa_unified_frame_lease *storage,
    const char *requested, qa_error *error) {
    static const char prefix[] = "No selected-arsenal grant mapping for ";
    size_t length = strlen(requested);
    if (length > SIZE_MAX - sizeof(prefix) - 1)
        return application_fail(error, QA_ERROR_MEMORY, "Selected grant message exceeds native extent");
    size_t bytes = sizeof(prefix) + length + 1;
    char *message = qa_unified_frame_lease_alloc(storage, bytes, 1, 1, error);
    if (!message) return false;
    snprintf(message, bytes, "%s%s\n", prefix, requested);
    qa_command_context context = {.owner = pair->source->owner, .actor = actor,
        .dialect = pair->source->component.clock.kind, .origin = QA_COMMAND_SERVER};
    return qa_application_log_emit(pair->owner->application, &context, message,
        QA_APPLICATION_LOG_CONSOLE, error);
}
bool application_supplies_cheat_ammo(void *context, qa_actor_id actor, qa_item_id source,
    bool set, int32_t amount, qa_error *error) {
    supply_pair *pair;
    if (!cheat_pair(context, actor, &pair, error)) return false;
    if (!pair) return true;
    size_t index = mapping_index(pair->profile.ammo, pair->profile.ammo_count, source);
    if (index == SIZE_MAX) return true;
    const qa_supply_mapping *grant = pair->profile.ammo + index;
    ++pair->owner->calls;
    bool okay = true;
    for (size_t i = 0; okay && i < grant->count; ++i) {
        qa_item_id item = grant->destinations[i];
        if (item == source) continue;
        qa_inventory_entry entry;
        okay = qa_inventory_entry_read(pair->owner->inventory, actor, item, &entry, error) &&
            supply_current(pair, actor, error);
        if (okay) okay = cheat_write(pair, actor, entry,
            set ? amount : qa_number_wrap_i32(entry.count + amount), error);
    }
    supplies_leave(pair->owner); return okay;
}
bool application_supplies_cheat_item(void *context, qa_actor_id actor, size_t argc,
    const char *const *argv, bool *handled, qa_error *error) {
    if (!handled || (argc && !argv))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected grant has no arguments or result");
    *handled = false;
    supply_pair *pair;
    if (!cheat_pair(context, actor, &pair, error)) return false;
    if (!pair || !argc) return true;
    application_supplies *owner = pair->owner;
    ++owner->calls;
    cheat_inventory view = {.storage = application_control_storage_acquire(owner->application, error)};
    bool quantity_present = false; double quantity = 0; char *requested = NULL;
    bool okay = view.storage && cheat_quantity(argv[argc - 1], &quantity_present, &quantity, error) &&
        cheat_request(view.storage, argc - quantity_present, argv, &requested, error);
    if (!okay) goto done;
    if (okay && pair->arsenal->kind == APPLICATION_PROVIDER_Q3) {
        okay = qa_q3_selected_holdable_give(pair->arsenal->state.q3, actor, requested, handled, error) &&
            supply_current(pair, actor, error);
        if (!okay || *handled) goto done;
    }
    if (okay) okay = cheat_inventory_read(pair, actor, &view, error);
    qa_strings *strings = qa_session_strings(owner->application->session);
    qa_item_id named = 0;
    qa_arena *storage = qa_unified_frame_lease_arena(view.storage);
    for (size_t i = 0; okay && !named && i < view.definition_count; ++i) {
        const qa_item_definition *definition = view.definitions + i;
        if (!cheat_arsenal_item(&view, definition->item)) continue;
        const char *identity = qa_strings_cstr(strings, definition->item); bool matches = false;
        okay = (!identity || cheat_name_equal(storage, identity, requested, &matches, error)) &&
            (matches || !definition->label || cheat_name_equal(storage, definition->label, requested, &matches, error));
        if (okay && matches) named = definition->item;
    }
    for (size_t i = 0; okay && i < view.entry_count; ++i) {
        qa_inventory_entry entry = view.entries[i];
        if (!cheat_arsenal_item(&view, entry.item)) continue;
        bool matches = entry.item == named;
        const char *identity = qa_strings_cstr(strings, entry.item);
        if (!matches && identity) {
            const char *base = strrchr(identity, '/');
            okay = cheat_name_equal(storage, identity, requested, &matches, error) &&
                (matches || cheat_name_equal(storage, base ? base + 1 : identity, requested, &matches, error));
        }
        if (okay && matches) {
            okay = cheat_write(pair, actor, entry, cheat_weapon(&view, entry.item) ? 1 :
                quantity_present ? quantity : entry.capacity, error);
            if (okay) *handled = true;
            goto done;
        }
    }
    if (okay) {
        qa_item_id requested_item = qa_strings_find(strings,
            (qa_bytes){(const uint8_t *)requested, strlen(requested)});
        size_t weapon_index = mapping_index(pair->profile.weapons, pair->profile.weapon_count, requested_item);
        size_t ammo_index = mapping_index(pair->profile.ammo, pair->profile.ammo_count, requested_item);
        bool weapon = weapon_index != SIZE_MAX;
        const qa_supply_mapping *grant = weapon ? pair->profile.weapons + weapon_index :
            ammo_index != SIZE_MAX ? pair->profile.ammo + ammo_index : NULL;
        const qa_q2_item_definition *source_definition = pair->source->kind == APPLICATION_PROVIDER_Q2 ?
            qa_q2_item_lookup(pair->source->state.q2, requested) : NULL;
        if (!grant) {
            if ((source_definition && (source_definition->kind == QA_Q2_ITEM_WEAPON || source_definition->kind == QA_Q2_ITEM_AMMO)) ||
                !strncmp(requested, "q1:weapon/", 10) || !strncmp(requested, "q1:ammo/", 8) ||
                !strncmp(requested, "rogue:ammo/", 11)) {
                okay = cheat_missing(pair, actor, view.storage,
                    source_definition ? source_definition->name : requested, error);
                if (okay) *handled = true;
            }
            goto done;
        }
        for (size_t i = 0; okay && i < grant->count; ++i) {
            qa_inventory_entry entry;
            okay = qa_inventory_entry_read(owner->inventory, actor, grant->destinations[i], &entry, error) &&
                supply_current(pair, actor, error);
            if (okay) okay = cheat_write(pair, actor, entry, weapon ? 1 : quantity_present ? quantity :
                source_definition && source_definition->kind == QA_Q2_ITEM_AMMO ? entry.count + source_definition->quantity : entry.capacity, error);
        }
        if (okay && weapon && pair->source->kind == APPLICATION_PROVIDER_Q2) {
            const qa_q2_item_definition *ammo = source_definition && source_definition->ammo ?
                qa_q2_item_lookup(pair->source->state.q2, qa_strings_cstr(strings, source_definition->ammo)) : NULL;
            size_t index = ammo ? mapping_index(pair->profile.ammo, pair->profile.ammo_count, ammo->item) : SIZE_MAX;
            if (index != SIZE_MAX) {
                qa_q2_combat_rules rules;
                okay = qa_q2_combat_rules_read(pair->source->state.q2, &rules) && supply_current(pair, actor, error);
                const qa_supply_mapping *ammo_grant = pair->profile.ammo + index;
                for (size_t i = 0; okay && i < ammo_grant->count; ++i)
                    okay = qa_inventory_give(owner->inventory, actor, ammo_grant->destinations[i],
                        rules.deathmatch_flags & 8192u ? 1000 : ammo->quantity, NULL, error) && supply_current(pair, actor, error);
            }
            if (okay) okay = weapon_granted(pair, actor, grant->destinations, grant->count, QA_PICKUP_SWITCH_IF_BETTER, error);
        }
        if (okay) *handled = true;
    }
done:
    if (okay && !*handled && (pair->arsenal->kind == APPLICATION_PROVIDER_Q1 ||
        pair->arsenal->kind == APPLICATION_PROVIDER_Q2 || pair->arsenal->kind == APPLICATION_PROVIDER_Q3 ||
        original_q3_arsenal(pair->arsenal)))
        okay = application_native_give_item(pair->source, actor, argc, argv, handled, error) &&
            supply_current(pair, actor, error);
    qa_unified_frame_lease_release(view.storage);
    supplies_leave(owner); return okay;
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
            supplies_leave(owner);
            return ok;
        }
        ++owner->calls;
        qa_item_id active;
        int32_t reference;
        float maximum;
        bool ok = application_guest_weapon_read(source, actor, &active, error) &&
            supply_current(pair, actor, error) &&
            application_qc_reference(source->state.qc.engine, actor, &reference, error) &&
            application_qc_float(source->state.qc.engine, reference, source->state.qc.engine->field_bindings->max_health, &maximum, error) &&
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
        supplies_leave(owner);
        return ok;
    }
    ++owner->calls;
    bool ok = true;
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    qa_unified_frame_lease *storage = application_control_storage_acquire(owner->application, error);
    ok = storage && qa_inventory_entries(owner->inventory, actor, NULL, 0, &count, error) &&
        supply_current(pair, actor, error);
    if (ok && count > SIZE_MAX / sizeof(*entries))
        ok = application_fail(error, QA_ERROR_MEMORY, "Selected arsenal reset exceeds native extent");
    if (ok && count) {
        entries = qa_unified_frame_lease_alloc(storage, count, sizeof(*entries),
            _Alignof(qa_inventory_entry), error);
        if (!entries) ok = false;
    }
    size_t read_count = 0;
    if (ok) ok = qa_inventory_entries(owner->inventory, actor, entries, count, &read_count, error) &&
        supply_current(pair, actor, error);
    if (ok && read_count > count)
        ok = application_fail(error, QA_ERROR_ARGUMENT,
            "Selected arsenal reset changed its retained inventory extent");
    for (size_t i = 0; ok && i < read_count; ++i) {
        bool selected;
        if (!arsenal_item(pair, entries[i].item, &selected, error)) { ok = false; break; }
        bool clear = selected && (arsenal->kind == APPLICATION_PROVIDER_Q3 || original_q3_arsenal(arsenal) ||
            (arsenal->kind == APPLICATION_PROVIDER_Q2 && family != QA_GAME_Q2));
        clear |= source->kind <= APPLICATION_PROVIDER_Q3 &&
            source_item(pair, entries[i].item) && !selected;
        if (clear) ok = starter_count(pair, actor, entries[i].item, 0, false, error);
    }
    qa_unified_frame_lease_release(storage);
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
        qa_item_id blaster = qa_q2_weapon_item(arsenal->state.q2, QA_Q2_BLASTER, false);
        if (family == QA_GAME_Q2) {
            double owned;
            ok = qa_inventory_count_read(owner->inventory, actor, blaster, &owned, error) &&
                supply_current(pair, actor, error);
            if (ok && owned == 0)
                ok = qa_inventory_give(owner->inventory, actor, blaster, 1, NULL, error) &&
                    supply_current(pair, actor, error);
        } else ok = starter_count(pair, actor, blaster, 1, true, error);
        if (ok && family == QA_GAME_Q1)
            ok = starter_count(pair, actor, qa_q2_weapon_item(arsenal->state.q2, QA_Q2_SHOTGUN, false), 1, true, error) &&
                starter_count(pair, actor, qa_q2_weapon_item(arsenal->state.q2, QA_Q2_SHOTGUN, true), 25, true, error);
        if (ok && family == QA_GAME_Q3)
            ok = starter_count(pair, actor, qa_q2_weapon_item(arsenal->state.q2, QA_Q2_MACHINEGUN, false), 1, true, error) &&
                starter_count(pair, actor, qa_q2_weapon_item(arsenal->state.q2, QA_Q2_CHAINGUN, false), 1, true, error) &&
                starter_count(pair, actor, qa_q2_weapon_item(arsenal->state.q2, QA_Q2_MACHINEGUN, true), 100, true, error);
        double active_count;
        if (ok) ok = qa_inventory_count_read(owner->inventory, actor,
            qa_q2_weapon_item(arsenal->state.q2, active, false), &active_count, error) && supply_current(pair, actor, error);
        if (ok && active_count < 1)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected Q2 starter weapon is not owned");
        if (ok) ok = qa_q2_weapon_restore(arsenal->state.q2, actor,
            &(qa_q2_weapon_state){.weapon = active, .phase = QA_Q2_ACTIVATING,
                .gun_rate = 10, .kick_seconds = 0.2f}, error) && supply_current(pair, actor, error) &&
            qa_q2_clear_input(arsenal->state.q2, actor, error) && supply_current(pair, actor, error);
    } else if (ok && (arsenal->kind == APPLICATION_PROVIDER_Q3 || original_q3_arsenal(arsenal))) {
        qa_q3_weapon active = family == QA_GAME_Q1 ? QA_Q3_W_SHOTGUN : QA_Q3_W_MACHINEGUN;
        q3_supply_arsenal roster;
        ok = q3_arsenal_read(pair, &roster, error);
        const application_q3_catalog_weapon *gauntlet = ok ? q3_arsenal_weapon(&roster, QA_Q3_W_GAUNTLET) : NULL;
        const application_q3_catalog_weapon *starter = ok ? q3_arsenal_weapon(&roster, active) : NULL;
        if (ok && (!gauntlet || !starter || !starter->ammo))
            ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Selected Q3 starter has no actual catalog weapon or ammunition");
        if (ok) ok = starter_count(pair, actor, gauntlet->item, 1, false, error) &&
            starter_count(pair, actor, starter->item, 1, false, error) &&
            starter_count(pair, actor, starter->ammo, family == QA_GAME_Q1 ? 25 : 100, false, error);
        qa_q3_player_state player;
        if (ok && arsenal->kind != APPLICATION_PROVIDER_Q3)
            ok = q3_request_weapon(pair, actor, starter, error);
        else if (ok && !qa_q3_player_read(arsenal->state.q3, actor, &player))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 starter lost its actual arsenal state");
        if (ok && arsenal->kind == APPLICATION_PROVIDER_Q3) {
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
    supplies_leave(owner);
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
            if (qa_q2_weapon_item(pair->arsenal->state.q2, (qa_q2_weapon)weapon, false) == items[i]) {
                incoming = (qa_q2_weapon)weapon; break;
            }
        if (incoming == QA_Q2_WEAPON_NONE)
            return application_fail(error, QA_ERROR_ARGUMENT, "Pickup receipt has no actual Q2 weapon definition");
        qa_item_id current = qa_q2_weapon_item(pair->arsenal->state.q2,
            state.pending != QA_Q2_WEAPON_NONE ? state.pending : state.weapon, false);
        if (selection == QA_PICKUP_SWITCH_ALWAYS || q2_rank(pair, items[i]) > q2_rank(pair, current)) {
            qa_q2_selection selected;
            if (!qa_q2_weapon_select(pair->arsenal->state.q2, actor, incoming, false, &selected, error) ||
                !supply_current(pair, actor, error)) return false;
        }
    }
    return true;
}
static bool q3_best_weapon(supply_pair *pair, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, int32_t *out, qa_error *error) {
    *out = QA_Q3_W_NONE;
    q3_supply_arsenal arsenal;
    if (!q3_arsenal_read(pair, &arsenal, error)) return false;
    int32_t ceiling = INT32_MAX;
    for (size_t i = 0; i < arsenal.count; ++i) {
        const application_q3_catalog_weapon *next = NULL;
        for (size_t j = 0; j < arsenal.count; ++j)
            if (arsenal.weapons[j].weapon < ceiling && (!next || arsenal.weapons[j].weapon > next->weapon))
                next = arsenal.weapons + j;
        if (!next) break;
        application_q3_catalog_weapon weapon = *next;
        ceiling = weapon.weapon;
        double owned;
        if (!qa_inventory_count_read(pair->owner->inventory, actor, weapon.item, &owned, error) ||
            !supply_current(pair, actor, error)) return false;
        if (owned <= 0) continue;
        if (weapon.ammo) {
            double available = 0; bool found = false;
            for (size_t j = 0; j < count; ++j)
                if (receipts[j].item == weapon.ammo) { available = receipts[j].before; found = true; break; }
            if (!found && (!qa_inventory_count_read(pair->owner->inventory, actor, weapon.ammo, &available, error) ||
                !supply_current(pair, actor, error))) return false;
            if (available <= 0) continue;
        }
        *out = weapon.weapon;
        break;
    }
    return true;
}
static bool q3_weapon_receipts(supply_pair *pair, qa_actor_id actor,
    const qa_item_id *items, size_t count, qa_pickup_selection_mode selection, qa_error *error) {
    if (selection == QA_PICKUP_SWITCH_NEVER) return true;
    int32_t active;
    q3_supply_arsenal arsenal;
    if (!q3_active_weapon(pair, actor, &active, error) || !q3_arsenal_read(pair, &arsenal, error)) return false;
    const application_q3_catalog_weapon *next = NULL;
    for (size_t i = 0; i < arsenal.count; ++i) {
        const application_q3_catalog_weapon *weapon = arsenal.weapons + i;
        if (next && weapon->weapon <= next->weapon) continue;
        for (size_t j = 0; j < count; ++j)
            if (items[j] == weapon->item) { next = weapon; break; }
    }
    return !next || (selection != QA_PICKUP_SWITCH_ALWAYS && next->weapon <= active) ||
        q3_request_weapon(pair, actor, next, error);
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
static bool ammo_apply(supply_pair *pair, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, bool auto_switch, bool refresh, qa_error *error) {
    if (!supply_current(pair, actor, error)) return false;
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        if (refresh)
            return qa_q1_selected_ammo_refresh(pair->arsenal->state.q1, actor, error) &&
                supply_current(pair, actor, error);
        qa_q1_auto_switch value;
        const qa_q1_auto_switch *preference;
        return q1_source_preference(pair, actor, &value, &preference, error) &&
            qa_q1_selected_pickup_ammo(pair->arsenal->state.q1, actor, receipts, count,
                auto_switch, preference, error) && supply_current(pair, actor, error);
    }
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_item_id grenades = qa_q2_weapon_item(pair->arsenal->state.q2, QA_Q2_GRENADES, true);
        for (size_t i = 0; auto_switch && i < count; ++i)
            if (receipts[i].item == grenades && receipts[i].before == 0)
                return q2_weapon_receipts(pair, actor, &grenades, 1, QA_PICKUP_SWITCH_IF_BETTER, error);
        return true;
    }
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3 || original_q3_arsenal(pair->arsenal)) {
        qa_q1_auto_switch value;
        const qa_q1_auto_switch *preference;
        if (!q1_source_preference(pair, actor, &value, &preference, error)) return false;
        if (preference && *preference == QA_Q1_SWITCH_NEVER) auto_switch = false;
        if (!auto_switch) return true;
        int32_t before, after, active;
        if (!q3_best_weapon(pair, actor, receipts, count, &before, error)) return false;
        if (!q3_active_weapon(pair, actor, &active, error)) return false;
        if (before == QA_Q3_W_NONE || active != before) return true;
        if (!q3_best_weapon(pair, actor, NULL, 0, &after, error)) return false;
        if (after == QA_Q3_W_NONE) return true;
        q3_supply_arsenal arsenal;
        if (!q3_arsenal_read(pair, &arsenal, error)) return false;
        const application_q3_catalog_weapon *weapon = q3_arsenal_weapon(&arsenal, after);
        return weapon && q3_request_weapon(pair, actor, weapon, error);
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Pickup receipt has no native selected arsenal");
}
static bool ammo_granted(void *context, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, bool auto_switch, qa_error *error) {
    return ammo_apply(context, actor, receipts, count, auto_switch, false, error);
}
bool application_supplies_horde_ammo(application_supplies *owner, application_provider *source,
    qa_actor_id actor, qa_horde_pickup_action action, qa_pickup_grant grant,
    bool *accepted, qa_error *error) {
    *accepted = false;
    application_provider *arsenal = application_provider_for(owner->application, actor, QA_ROLE_ARSENAL, "");
    if (!arsenal) return true;
    supply_pair *pair = pair_find(owner, source, arsenal);
    if (!pair) return application_fail(error, QA_ERROR_ARGUMENT, "Horde ammunition lost its admitted supply pair");
    ++owner->calls;
    bool ok;
    if (action == QA_HORDE_AMMO_PRESELECT) {
        ok = ammo_apply(pair, actor, NULL, 0, true, false, error);
        if (ok) *accepted = true;
    } else {
        qa_supply *selected;
        ok = qa_q1_player_supply(source->state.q1, actor, &selected, error);
        qa_supply_offer offer = {.kind = QA_SUPPLY_AMMO, .ammo = &grant, .ammo_count = 1};
        if (action == QA_HORDE_AMMO_PREVIEW) {
            qa_supply_preview_result result = {0};
            if (ok) ok = qa_supply_preview(selected, actor, &offer, false, &result, error);
            if (ok) *accepted = result.accepted;
            qa_supply_preview_free(&result);
        } else {
            if (ok) ok = qa_supply_ammo(selected, actor, grant, false, accepted, error) &&
                ammo_apply(pair, actor, NULL, 0, false, true, error);
        }
    }
    supplies_leave(owner);
    return ok;
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
    if (pair->arsenal->kind == APPLICATION_PROVIDER_Q3 || original_q3_arsenal(pair->arsenal))
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
    const qa_q3_item *items = qa_q3_items((source->product->campaign_id == QA_CAMPAIGN_MISSIONPACK)
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
    int limit = (source->product->campaign_id == QA_CAMPAIGN_MISSIONPACK)
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
    supplies_leave(owner); return ok;
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
    supplies_leave(owner); return ok;
}

static bool timers_require(supply_pair *pair, qa_actor_id actor, supply_actor **out, qa_error *error) {
    supply_actor *entry = actor_find(pair->owner, pair, actor);
    if (!entry || !supply_current(pair, actor, error))
        return entry ? false : application_fail(error, QA_ERROR_ARGUMENT, "Mapped ammo actor is not admitted");
    if (entry->timers_ready) { *out = entry; return true; }
    size_t count = pair->profile.ammo_owner_count;
    supply_timer *timers = entry->timers;
    for (size_t i = 0; i < count; ++i) {
        qa_supply_source_owner binding = pair->profile.ammo_owners[i];
        timers[i] = (supply_timer){.item = binding.item, .source = binding.source,
            .weapon = timer_weapon(pair, binding.source)};
    }
    entry->timer_count = count; entry->timers_ready = true;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_inventory_entry resource;
        ok = qa_inventory_entry_read(pair->owner->inventory, actor, timers[i].item, &resource, error) &&
            supply_current(pair, actor, error) && actor_find(pair->owner, pair, actor) == entry;
    }
    if (!ok) {
        if (actor_find(pair->owner, pair, actor) == entry) {
            entry->timer_count = 0; entry->timers_ready = false;
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
    supplies_leave(owner); return ok;
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
    /* Source stores do not activate regeneration counters before their first use. */
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
static bool roster_matches(application_supplies *owner, const supply_actor *saved, qa_error *error) {
    const struct application_player_roster *roster = owner->application->players;
    qa_actor_index index = {0};
    if (!qa_actor_index_create(&index, qa_session_actors(owner->application->session), NULL, error)) return false;
    size_t expected = 0, actual = 0;
    bool ok = true;
    for (const supply_actor *row = saved; ok && row; row = row->next) {
        ++actual;
        if (!pair_current(row->pair, row->actor, error)) { ok = false; break; }
        size_t prior;
        if (qa_actor_index_find(&index, row->actor, &prior))
            ok = application_fail(error, QA_ERROR_FORMAT, "Duplicate saved supply actor admission");
        else qa_actor_index_put(&index, row->actor, (size_t)(uintptr_t)row);
    }
    for (size_t i = 0; ok && roster && i < roster->count; ++i) {
        const application_player_record *player = roster->records + i;
        if (!player->actor.registry || player->retiring) continue;
        ++expected;
        size_t value;
        const supply_actor *row = qa_actor_index_find(&index, player->actor, &value)
            ? (const supply_actor *)(uintptr_t)value : NULL;
        if (!row || row->pair->source != roster->map_provider)
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved supply admission omits an actual source player");
    }
    if (ok && expected != actual) ok = application_fail(error, QA_ERROR_FORMAT,
        "Saved supply admission contains a player outside the actual source roster");
    qa_actor_index_destroy(&index);
    return ok;
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
        supply_actor *row = reading ? NULL : *link;
        size_t ordinal = 0;
        if (!reading) {
            supply_pair *pair = owner->pairs;
            while (pair && pair != row->pair) { pair = pair->next; ++ordinal; }
        }
        if (!pairs || !qa_source_save_count(io, &ordinal, pairs - 1))
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply actor has no prepared source pair");
        if (reading) {
            row = actor_take(pair_ordinal(owner, ordinal), error);
            if (!row) return false;
            *link = row;
        }
        if (!qa_source_save_actor(io, &row->actor)) return false;
        if (!row->actor.registry)
            return application_fail(error, QA_ERROR_FORMAT, "Saved supply admission has no actual actor");
        if (!qa_source_save_bool(io, &row->timers_ready) ||
            !qa_source_save_count(io, &row->timer_count, row->pair->profile.ammo_owner_count)) return false;
        if (reading && row->timer_count) {
            if (row->timer_count > (io->input.size - io->offset) / 12)
                return application_fail(error, QA_ERROR_FORMAT, "Truncated saved mapped ammo pools");
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
    qa_unified_frame_lease *storage = application_control_storage_acquire(app, error);
    if (!storage) return false;
    supply_actor *copy = NULL, **tail = &copy;
    for (const supply_actor *row = owner->actors; row; row = row->next) {
        *tail = qa_unified_frame_lease_alloc(storage, 1, sizeof(**tail),
            _Alignof(supply_actor), error);
        if (!*tail) { qa_unified_frame_lease_release(storage); return false; }
        **tail = (supply_actor){0};
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
                row->timers = qa_unified_frame_lease_alloc(storage, row->timer_count,
                    sizeof(*row->timers), _Alignof(supply_timer), error);
                if (!row->timers) { ok = false; break; }
                memcpy(row->timers, actual->timers, row->timer_count * sizeof(*row->timers));
            }
        }
    }
    qa_source_save_io io = {0};
    qa_buffer result = {0};
    if (ok) ok = qa_source_save_writer(&io, app->session, error) && supply_signature(&io, error) &&
        supply_pairs_field(owner, &io, error) && supply_actors_field(owner, &io, &copy, error) &&
        capture_current(owner, copy, error) && qa_source_save_finish(&io, &result);
    qa_source_save_dispose(&io); qa_unified_frame_lease_release(storage); supplies_leave(owner);
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
        for (supply_pair *pair = owner->pairs; pair; pair = pair->next) qa_actor_index_clear(&pair->actors);
        supply_actor **link = &owner->actors;
        for (supply_actor *row = owner->actors; row; row = row->next) {
            row->previous = link;
            qa_actor_index_put(&row->pair->actors, row->actor, (size_t)(uintptr_t)row);
            link = &row->next;
        }
    }
    actor_list_free(decoded);
    supplies_leave(owner);
    return ok;
}
