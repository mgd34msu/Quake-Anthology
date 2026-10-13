#include "qa/q2_sound.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>

#define VIS(c, n, m, i, s, r)                                                                      \
    .classname = c, .name = n, .model = m, .icon = i, .sound = s, .respawn_seconds = r
#define AMMO(c, n, m, i, q, cap, rot)                                                              \
    {VIS(c, n, m, i, QA_Q2_SOUND_MISC_AM_PKUP, 30),                                                      \
     .kind = QA_Q2_ITEM_AMMO,                                                                      \
     .quantity = q,                                                                                \
     .capacity = cap,                                                                              \
     .droppable = true,                                                                            \
     .rotate = rot,                                                                                \
     .infinite_quantity = true}
#define KEY(c, n, m, i)                                                                            \
    {VIS(c, n, m, i, QA_Q2_SOUND_ITEMS_PKUP, 0),                                                         \
     .kind = QA_Q2_ITEM_KEY,                                                                       \
     .quantity = 1,                                                                                \
     .capacity = 32767,                                                                            \
     .coop_stay = true,                                                                            \
     .droppable = true,                                                                            \
     .rotate = true}
#define POWER(c, n, m, i, t, p)                                                                    \
    {VIS(c, n, m, i, QA_Q2_SOUND_ITEMS_PKUP, t),                                                         \
     .kind = QA_Q2_ITEM_POWER,                                                                     \
     .powerup = p,                                                                                 \
     .quantity = 1,                                                                                \
     .capacity = 32767,                                                                            \
     .droppable = true,                                                                            \
     .rotate = true}
#define HEALTH(c, m, s, q, over, mega)                                                             \
    {VIS(c, "Health", m, "i_health", s, mega ? 20 : 30), .kind = QA_Q2_ITEM_HEALTH, .quantity = q, \
     .ignore_maximum = over, .timed = mega}
#define ARMOR(c, n, m, i, q, cap, norm, energy)                                                    \
    {VIS(c, n, m, i, QA_Q2_SOUND_MISC_AR1_PKUP, 20),                                                     \
     .kind = QA_Q2_ITEM_ARMOR,                                                                     \
     .quantity = q,                                                                                \
     .capacity = cap,                                                                              \
     .normal_protection = norm,                                                                    \
     .energy_protection = energy,                                                                  \
     .rotate = true}
static const qa_q2_item_definition base[] = {
    AMMO("ammo_shells", "Shells", "models/items/ammo/shells/medium/tris.md2", "a_shells", 10, 100,
         false),
    AMMO("ammo_bullets", "Bullets", "models/items/ammo/bullets/medium/tris.md2", "a_bullets", 50,
         200, false),
    AMMO("ammo_cells", "Cells", "models/items/ammo/cells/medium/tris.md2", "a_cells", 50, 200,
         false),
    AMMO("ammo_rockets", "Rockets", "models/items/ammo/rockets/medium/tris.md2", "a_rockets", 5, 50,
         false),
    AMMO("ammo_slugs", "Slugs", "models/items/ammo/slugs/medium/tris.md2", "a_slugs", 10, 50,
         false),
    AMMO("ammo_grenades", "Grenades", "models/items/ammo/grenades/medium/tris.md2", "a_grenades", 5,
         50, true),
    KEY("key_data_cd", "Data CD", "models/items/keys/data_cd/tris.md2", "k_datacd"),
    KEY("key_power_cube", "Power Cube", "models/items/keys/power/tris.md2", "k_powercube"),
    KEY("key_pyramid", "Pyramid Key", "models/items/keys/pyramid/tris.md2", "k_pyramid"),
    KEY("key_data_spinner", "Data Spinner", "models/items/keys/spinner/tris.md2", "k_dataspin"),
    KEY("key_pass", "Security Pass", "models/items/keys/pass/tris.md2", "k_security"),
    KEY("key_blue_key", "Blue Key", "models/items/keys/key/tris.md2", "k_bluekey"),
    KEY("key_red_key", "Red Key", "models/items/keys/red_key/tris.md2", "k_redkey"),
    KEY("key_commander_head", "Commander's Head", "models/monsters/commandr/head/tris.md2",
        "k_comhead"),
    KEY("key_airstrike_target", "Airstrike Marker", "models/items/keys/target/tris.md2",
        "i_airstrike"),
    {VIS("item_bandolier", "Bandolier", "models/items/band/tris.md2", "p_bandolier",
         QA_Q2_SOUND_ITEMS_PKUP, 60),
     .kind = QA_Q2_ITEM_PACK, .rotate = true},
    {VIS("item_pack", "Ammo Pack", "models/items/pack/tris.md2", "i_pack", QA_Q2_SOUND_ITEMS_PKUP, 180),
     .kind = QA_Q2_ITEM_PACK, .rotate = true, .full_pack = true},
    HEALTH("item_health", "models/items/healing/medium/tris.md2", QA_Q2_SOUND_ITEMS_N_HEALTH, 10, false,
           false),
    HEALTH("item_health_small", "models/items/healing/stimpack/tris.md2", QA_Q2_SOUND_ITEMS_S_HEALTH, 2,
           true, false),
    HEALTH("item_health_large", "models/items/healing/large/tris.md2", QA_Q2_SOUND_ITEMS_L_HEALTH, 25,
           false, false),
    HEALTH("item_health_mega", "models/items/mega_h/tris.md2", QA_Q2_SOUND_ITEMS_M_HEALTH, 100, true,
           true),
    ARMOR("item_armor_jacket", "Jacket Armor", "models/items/armor/jacket/tris.md2",
          "i_jacketarmor", 25, 50, .3f, 0),
    ARMOR("item_armor_combat", "Combat Armor", "models/items/armor/combat/tris.md2",
          "i_combatarmor", 50, 100, .6f, .3f),
    ARMOR("item_armor_body", "Body Armor", "models/items/armor/body/tris.md2", "i_bodyarmor", 100,
          200, .8f, .6f),
    {VIS("item_armor_shard", "Armor Shard", "models/items/armor/shard/tris.md2", "i_jacketarmor",
         QA_Q2_SOUND_MISC_AR2_PKUP, 20),
     .kind = QA_Q2_ITEM_SHARD, .quantity = 2, .rotate = true},
    POWER("item_quad", "Quad Damage", "models/items/quaddama/tris.md2", "p_quad", 60,
          QA_Q2_POWER_QUAD),
    POWER("item_invulnerability", "Invulnerability", "models/items/invulner/tris.md2",
          "p_invulnerability", 300, QA_Q2_POWER_INVULNERABILITY),
    POWER("item_silencer", "Silencer", "models/items/silencer/tris.md2", "p_silencer", 60,
          QA_Q2_POWER_SILENCER),
    POWER("item_breather", "Rebreather", "models/items/breather/tris.md2", "p_rebreather", 60,
          QA_Q2_POWER_BREATHER),
    POWER("item_enviro", "Environment Suit", "models/items/enviro/tris.md2", "p_envirosuit", 60,
          QA_Q2_POWER_ENVIRO),
    {VIS("item_power_screen", "Power Screen", "models/items/armor/screen/tris.md2", "i_powerscreen",
         QA_Q2_SOUND_MISC_AR3_PKUP, 60),
     .kind = QA_Q2_ITEM_POWER_ARMOR, .powered_armor = QA_POWER_SCREEN, .capacity = 32767,
     .quantity = 1, .droppable = true, .rotate = true},
    {VIS("item_power_shield", "Power Shield", "models/items/armor/shield/tris.md2", "i_powershield",
         QA_Q2_SOUND_MISC_AR3_PKUP, 60),
     .kind = QA_Q2_ITEM_POWER_ARMOR, .powered_armor = QA_POWER_SHIELD, .capacity = 32767,
     .quantity = 1, .droppable = true, .rotate = true},
    {VIS("item_adrenaline", "Adrenaline", "models/items/adrenal/tris.md2", "p_adrenaline",
         QA_Q2_SOUND_ITEMS_PKUP, 60),
     .kind = QA_Q2_ITEM_MAX_HEALTH, .quantity = 1, .fill = true, .rotate = true},
    {VIS("item_ancient_head", "Ancient Head", "models/items/c_head/tris.md2", "i_fixme",
         QA_Q2_SOUND_ITEMS_PKUP, 60),
     .kind = QA_Q2_ITEM_MAX_HEALTH, .quantity = 2, .rotate = true}};
static const qa_q2_item_definition xatrix[] = {
    AMMO("ammo_magslug", "Mag Slug", "models/objects/ammo/tris.md2", "a_mslugs", 10, 50, false),
    AMMO("ammo_trap", "Trap", "models/weapons/g_trap/tris.md2", "a_trap", 1, 5, true),
    KEY("key_green_key", "Green Key", "models/items/keys/green_key/tris.md2", "k_green"),
    POWER("item_quadfire", "DualFire Damage", "models/items/quadfire/tris.md2", "p_quadfire", 60,
          QA_Q2_POWER_QUADFIRE),
    {VIS("item_foodcube", "Health", "models/objects/trapfx/tris.md2", "i_health",
         QA_Q2_SOUND_ITEMS_S_HEALTH, 0),
     .kind = QA_Q2_ITEM_FOOD}};
static const qa_q2_item_definition rogue[] = {
    AMMO("ammo_flechettes", "Flechettes", "models/ammo/am_flechette/tris.md2", "a_flechettes", 50,
         200, false),
    AMMO("ammo_prox", "Prox", "models/ammo/am_prox/tris.md2", "a_prox", 5, 50, false),
    AMMO("ammo_tesla", "Tesla", "models/ammo/am_tesl/tris.md2", "a_tesla", 5, 50, true),
    AMMO("ammo_disruptor", "Rounds", "models/ammo/am_disr/tris.md2", "a_disruptor", 15, 100, false),
    KEY("key_nuke_container", "Antimatter Pod", "models/weapons/g_nuke/tris.md2", "i_contain"),
    KEY("key_nuke", "Antimatter Bomb", "models/weapons/g_nuke/tris.md2", "i_nuke"),
    POWER("item_double", "Double Damage", "models/items/ddamage/tris.md2", "p_double", 60,
          QA_Q2_POWER_DOUBLE),
    POWER("item_ir_goggles", "IR Goggles", "models/items/goggles/tris.md2", "p_ir", 60,
          QA_Q2_POWER_IR),
    {VIS("item_doppleganger", "Doppleganger", "models/items/dopple/tris.md2", "p_doppleganger",
         QA_Q2_SOUND_ITEMS_PKUP, 90),
     .kind = QA_Q2_ITEM_DECOY, .quantity = 1, .capacity = 1, .droppable = true, .rotate = true},
    {VIS("ammo_nuke", "A-M Bomb", "models/weapons/g_nuke/tris.md2", "p_nuke", QA_Q2_SOUND_MISC_AM_PKUP,
         300),
     .kind = QA_Q2_ITEM_NUKE, .quantity = 1, .capacity = 1, .droppable = true, .rotate = true},
    {VIS("item_sphere_defender", "defender sphere", "models/items/defender/tris.md2", "p_defender",
         QA_Q2_SOUND_ITEMS_PKUP, 60),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_sphere_hunter", "hunter sphere", "models/items/hunter/tris.md2", "p_hunter",
         QA_Q2_SOUND_ITEMS_PKUP, 120),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_sphere_vengeance", "vengeance sphere", "models/items/vengnce/tris.md2",
         "p_vengeance", QA_Q2_SOUND_ITEMS_PKUP, 60),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_compass", "compass", "models/objects/fire/tris.md2", "p_compass", QA_Q2_SOUND_ITEMS_PKUP,
         60),
     .kind = QA_Q2_ITEM_COMPASS, .quantity = 1, .capacity = 32767, .rotate = true}};
static const qa_q2_item_definition rerelease[] = {
    POWER("item_invisibility", "Invisibility", "models/items/cloaker/tris.md2", "p_cloaker", 300,
          QA_Q2_POWER_INVISIBILITY),
    {VIS("item_flashlight", "Flashlight", "models/items/flashlight/tris.md2", "p_torch",
         QA_Q2_SOUND_ITEMS_PKUP, 0),
     .kind = QA_Q2_ITEM_FLASHLIGHT, .quantity = 1, .capacity = 1, .coop_stay = true,
     .rotate = true},
    KEY("key_explosive_charges", "Explosive Charges", "models/items/n64/charge/tris.md2",
        "n64/i_charges"),
    KEY("key_yellow_key", "Yellow Key", "models/items/n64/yellow_key/tris.md2", "n64/i_yellow_key"),
    KEY("key_power_core", "Power Core", "models/items/n64/power_core/tris.md2", "k_pyramid")};
#undef VIS
#undef AMMO
#undef KEY
#undef POWER
#undef HEALTH
#undef ARMOR
#define N(a) (sizeof(a) / sizeof(*(a)))
static void install(q2_items *items, const qa_q2_item_definition *definitions, size_t count) {
    memcpy(items->definitions + items->count, definitions, count * sizeof(*definitions));
    items->count += count;
}
static void bind_rules(qa_q2_item_definition *d) {
    static const struct { const char *classname; uint32_t flag; } rules[] = {
        {"key_power_cube", QA_Q2_ITEM_POWER_CUBE},
        {"key_explosive_charges", QA_Q2_ITEM_EXPLOSIVE_CHARGES},
        {"ammo_prox", QA_Q2_ITEM_PROX_AMMO}, {"ammo_tesla", QA_Q2_ITEM_TESLA_AMMO},
        {"ammo_trap", QA_Q2_ITEM_TRAP_AMMO}, {"ammo_disruptor", QA_Q2_ITEM_DISRUPTOR_AMMO},
        {"key_commander_head", QA_Q2_ITEM_COMMANDER_HEAD},
        {"item_sphere_defender", QA_Q2_ITEM_SPHERE_DEFENDER},
        {"item_sphere_hunter", QA_Q2_ITEM_SPHERE_HUNTER},
        {"item_sphere_vengeance", QA_Q2_ITEM_SPHERE_VENGEANCE}
    };
    d->rule_flags = 0; d->disabled_weapon_mask = 0;
    for (size_t i = 0; i < sizeof(rules) / sizeof(*rules); ++i)
        if (!strcmp(d->classname, rules[i].classname)) d->rule_flags |= rules[i].flag;
    if (d->kind != QA_Q2_ITEM_WEAPON) return;
    switch (d->weapon) {
    case QA_Q2_BFG: d->disabled_weapon_mask = 1; break;
    case QA_Q2_HYPERBLASTER: d->disabled_weapon_mask = 2; break;
    case QA_Q2_RAILGUN: d->disabled_weapon_mask = 4; break;
    case QA_Q2_ROCKETLAUNCHER: d->disabled_weapon_mask = 8; break;
    case QA_Q2_GRENADELAUNCHER: d->disabled_weapon_mask = 16; break;
    case QA_Q2_CHAINGUN: d->disabled_weapon_mask = 32; break;
    case QA_Q2_MACHINEGUN: d->disabled_weapon_mask = 64; break;
    case QA_Q2_SUPERSHOTGUN: d->disabled_weapon_mask = 128; break;
    case QA_Q2_SHOTGUN: d->disabled_weapon_mask = 256; break;
    case QA_Q2_LMCTF_PLASMA: d->disabled_weapon_mask = 512; break;
    default: break;
    }
}
static bool bind_localized_names(qa_q2_game *g, qa_q2_item_definition *d, qa_error *e) {
    d->localized_name_id = d->definite_name_id = d->name_id;
    if (g->options.rules != QA_RULESET_Q2_RERELEASE) return true;
    static const struct { const char *classname, *name, *definite; } names[] = {
        {"item_armor_body", "$item_body_armor", "$item_body_armor_def"},
        {"item_armor_combat", "$item_combat_armor", "$item_combat_armor_def"},
        {"item_armor_jacket", "$item_jacket_armor", "$item_jacket_armor_def"},
        {"item_armor_shard", "$item_armor_shard", "$item_armor_shard_def"},
        {"item_power_screen", "$item_power_screen", "$item_power_screen_def"},
        {"item_power_shield", "$item_power_shield", "$item_power_shield_def"},
        {"weapon_grapple", "$item_grapple", "$item_grapple_def"},
        {"weapon_blaster", "$item_blaster", "$item_blaster_def"},
        {"weapon_chainfist", "$item_chainfist", "$item_chainfist_def"},
        {"weapon_shotgun", "$item_shotgun", "$item_shotgun_def"},
        {"weapon_supershotgun", "$item_super_shotgun", "$item_super_shotgun_def"},
        {"weapon_machinegun", "$item_machinegun", "$item_machinegun_def"},
        {"weapon_etf_rifle", "$item_etf_rifle", "$item_etf_rifle_def"},
        {"weapon_chaingun", "$item_chaingun", "$item_chaingun_def"},
        {"ammo_grenades", "$item_grenades", "$item_grenades_def"},
        {"ammo_trap", "$item_trap", "$item_trap_def"},
        {"ammo_tesla", "$item_tesla", "$item_tesla_def"},
        {"weapon_grenadelauncher", "$item_grenade_launcher", "$item_grenade_launcher_def"},
        {"weapon_proxlauncher", "$item_prox_launcher", "$item_prox_launcher_def"},
        {"weapon_rocketlauncher", "$item_rocket_launcher", "$item_rocket_launcher_def"},
        {"weapon_hyperblaster", "$item_hyperblaster", "$item_hyperblaster_def"},
        {"weapon_boomer", "$item_ionripper", "$item_ionripper_def"},
        {"weapon_plasmabeam", "$item_plasma_beam", "$item_plasma_beam_def"},
        {"weapon_railgun", "$item_railgun", "$item_railgun_def"},
        {"weapon_phalanx", "$item_phalanx", "$item_phalanx_def"},
        {"weapon_bfg", "$item_bfg10k", "$item_bfg10k_def"},
        {"weapon_disintegrator", "$item_disruptor", "$item_disruptor_def"},
        {"ammo_shells", "$item_shells", "$item_shells_def"},
        {"ammo_bullets", "$item_bullets", "$item_bullets_def"},
        {"ammo_cells", "$item_cells", "$item_cells_def"},
        {"ammo_rockets", "$item_rockets", "$item_rockets_def"},
        {"ammo_slugs", "$item_slugs", "$item_slugs_def"},
        {"ammo_magslug", "$item_mag_slug", "$item_mag_slug_def"},
        {"ammo_flechettes", "$item_flechettes", "$item_flechettes_def"},
        {"ammo_prox", "$item_prox", "$item_prox_def"},
        {"ammo_nuke", "$item_am_bomb", "$item_am_bomb_def"},
        {"ammo_disruptor", "$item_rounds", "$item_rounds_def"},
        {"item_quad", "$item_quad_damage", "$item_quad_damage_def"},
        {"item_quadfire", "$item_dualfire_damage", "$item_dualfire_damage_def"},
        {"item_invulnerability", "$item_invulnerability", "$item_invulnerability_def"},
        {"item_invisibility", "$item_invisibility", "$item_invisibility_def"},
        {"item_silencer", "$item_silencer", "$item_silencer_def"},
        {"item_breather", "$item_rebreather", "$item_rebreather_def"},
        {"item_enviro", "$item_environment_suit", "$item_environment_suit_def"},
        {"item_ancient_head", "$item_ancient_head", "$item_ancient_head_def"},
        {"item_adrenaline", "$item_adrenaline", "$item_adrenaline_def"},
        {"item_bandolier", "$item_bandolier", "$item_bandolier_def"},
        {"item_pack", "$item_ammo_pack", "$item_ammo_pack_def"},
        {"item_ir_goggles", "$item_ir_goggles", "$item_ir_goggles_def"},
        {"item_double", "$item_double_damage", "$item_double_damage_def"},
        {"item_sphere_vengeance", "$item_vengeance_sphere", "$item_vengeance_sphere_def"},
        {"item_sphere_hunter", "$item_hunter_sphere", "$item_hunter_sphere_def"},
        {"item_sphere_defender", "$item_defender_sphere", "$item_defender_sphere_def"},
        {"item_doppleganger", "$item_doppleganger", "$item_doppleganger_def"},
        {"key_data_cd", "$item_data_cd", "$item_data_cd_def"},
        {"key_power_cube", "$item_power_cube", "$item_power_cube_def"},
        {"key_explosive_charges", "$item_explosive_charges", "$item_explosive_charges_def"},
        {"key_yellow_key", "$item_yellow_key", "$item_yellow_key_def"},
        {"key_power_core", "$item_power_core", "$item_power_core_def"},
        {"key_pyramid", "$item_pyramid_key", "$item_pyramid_key_def"},
        {"key_data_spinner", "$item_data_spinner", "$item_data_spinner_def"},
        {"key_pass", "$item_security_pass", "$item_security_pass_def"},
        {"key_blue_key", "$item_blue_key", "$item_blue_key_def"},
        {"key_red_key", "$item_red_key", "$item_red_key_def"},
        {"key_green_key", "$item_green_key", "$item_green_key_def"},
        {"key_commander_head", "$item_commanders_head", "$item_commanders_head_def"},
        {"key_airstrike_target", "$item_airstrike_marker", "$item_airstrike_marker_def"},
        {"key_nuke_container", "$item_antimatter_pod", "$item_antimatter_pod_def"},
        {"key_nuke", "$item_antimatter_bomb", "$item_antimatter_bomb_def"},
        {"item_health_small", "$item_stimpack", "$item_stimpack_def"},
        {"item_health", "$item_small_medkit", "$item_small_medkit_def"},
        {"item_health_large", "$item_large_medkit", "$item_large_medkit"},
        {"item_health_mega", "$item_mega_health", "$item_mega_health_def"},
        {"item_flashlight", "$item_flashlight", "$item_flashlight_def"},
        {"item_compass", "$item_compass", "$item_compass_def"},
    };
    for (size_t i = 0; i < N(names); ++i) {
        if (strcmp(d->classname, names[i].classname)) continue;
        return qa_builtin_resource(&g->services, names[i].name, &d->localized_name_id, e) &&
            qa_builtin_resource(&g->services, names[i].definite, &d->definite_name_id, e);
    }
    return true;
}
static bool same(const char *a, const char *b) {
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
size_t qa_q2_item_count(const qa_q2_game *g) {
    return g && g->item_runtime ? g->item_runtime->count : 0;
}
const qa_q2_item_definition *qa_q2_item_at(const qa_q2_game *g, size_t index) {
    return index < qa_q2_item_count(g) ? &g->item_runtime->definitions[index] : NULL;
}
const qa_q2_item_definition *qa_q2_item_lookup(const qa_q2_game *g, const char *name) {
    if (!name)
        return NULL;
    if (!strncmp(name, "q2:", 3))
        name += 3;
    for (size_t i = 0; i < qa_q2_item_count(g); ++i) {
        const qa_q2_item_definition *d = qa_q2_item_at(g, i);
        if (same(name, d->classname) || same(name, d->name))
            return d;
    }
    return NULL;
}
static int item_id_order(const void *left,const void *right) {
    qa_item_id a=(*(const qa_q2_item_definition *const *)left)->item;
    qa_item_id b=(*(const qa_q2_item_definition *const *)right)->item;
    return (a>b)-(a<b);
}
const qa_q2_item_definition *q2_item_by_id(qa_q2_game *g, qa_item_id id) {
    q2_items *r=g->item_runtime;
    size_t first=0,last=r->count;
    while (first<last) {
        size_t mid=first+(last-first)/2;
        const qa_q2_item_definition *d=r->by_id[mid];
        if (d->item==id) return d;
        if (d->item<id) first=mid+1; else last=mid;
    }
    return NULL;
}
bool qa_q2_item_authored_bind(qa_q2_game *g, qa_string_id name, qa_error *e) {
    qa_id_index *index = &g->item_runtime->authored_items;
    if (!name || qa_id_index_find(index, name, 0)) return true;
    const char *text = qa_strings_cstr(qa_session_strings(g->services.session), name);
    const qa_q2_item_definition *definition = text ? qa_q2_item_lookup(g, text) : NULL;
    if (!qa_id_index_reserve(index, 1, e)) return false;
    qa_id_index_insert(index, name, 0, definition ? definition->item : 0);
    return true;
}
qa_item_id qa_q2_item_authored_id(qa_q2_game *g, qa_string_id name) {
    qa_id_index_entry *entry = qa_id_index_find(&g->item_runtime->authored_items, name, 0);
    return entry ? (qa_item_id)entry->value : 0;
}
bool q2_supplemental_find(qa_q2_game *g, const char *name, bool names_only,
                          qa_q2_supplemental_item *out) {
    const qa_q2_item_options *s = &g->item_runtime->options;
    if (!name || !s->supplemental_count || !s->supplemental_item)
        return false;
    size_t count = s->supplemental_count(s->context);
    for (size_t i = 0; i < count; ++i) {
        qa_q2_supplemental_item item;
        if (!s->supplemental_item(s->context, i, &item))
            continue;
        const char *id =
            qa_strings_cstr(qa_session_strings(g->services.session), item.definition.item);
        if (same(name, item.definition.label) ||
            (!names_only && (same(name, item.classname) || (id && same(name, id))))) {
            *out = item;
            return true;
        }
    }
    return false;
}
const char *qa_q2_weapon_display_name(qa_q2_weapon weapon) {
    static const char *const names[QA_Q2_WEAPON_COUNT] = {NULL,
                                                          "Blaster",
                                                          "Shotgun",
                                                          "Super Shotgun",
                                                          "Machinegun",
                                                          "Chaingun",
                                                          "Grenades",
                                                          "Grenade Launcher",
                                                          "Rocket Launcher",
                                                          "HyperBlaster",
                                                          "Railgun",
                                                          "BFG10K",
                                                          "Trap",
                                                          "Ionripper",
                                                          "Phalanx",
                                                          "Tesla",
                                                          "Prox Launcher",
                                                          "Chainfist",
                                                          "Disruptor",
                                                          "ETF Rifle",
                                                          "Plasma Beam",
                                                          "Grapple",
                                                          "Grappling Hook",
                                                          "Plasma Rifle"};
    return weapon > QA_Q2_WEAPON_NONE && weapon < QA_Q2_WEAPON_COUNT ? names[weapon] : NULL;
}
bool q2_item_catalog(qa_q2_game *g, qa_error *e) {
    q2_items *r = g->item_runtime;
    r->definitions = calloc(N(base) + N(xatrix) + N(rogue) + N(rerelease) + QA_Q2_WEAPON_COUNT,
                            sizeof(*r->definitions));
    if (!r->definitions) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 item catalog");
        return false;
    }
    install(r, base, N(base));
    if (g->options.product == QA_Q2_XATRIX || g->options.rules == QA_RULESET_Q2_RERELEASE)
        install(r, xatrix, N(xatrix));
    if (g->options.product == QA_Q2_ROGUE || g->options.rules == QA_RULESET_Q2_RERELEASE)
        install(r, rogue, N(rogue));
    if (g->options.rules == QA_RULESET_Q2_RERELEASE) {
        install(r, rerelease, N(rerelease));
        static const char *const key_order[] = {
            "key_data_cd", "key_power_cube", "key_explosive_charges", "key_yellow_key",
            "key_power_core", "key_pyramid", "key_data_spinner", "key_pass", "key_blue_key",
            "key_red_key", "key_green_key", "key_commander_head", "key_airstrike_target",
            "key_nuke_container", "key_nuke"};
        size_t key = 0;
        for (size_t i = 0; i < r->count; ++i) {
            if (r->definitions[i].kind != QA_Q2_ITEM_KEY) continue;
            for (size_t j = i; j < r->count; ++j) {
                if (strcmp(r->definitions[j].classname, key_order[key])) continue;
                qa_q2_item_definition swap = r->definitions[i];
                r->definitions[i] = r->definitions[j];
                r->definitions[j] = swap;
                break;
            }
            ++key;
        }
    }
    static const char *const icons[QA_Q2_WEAPON_COUNT] = {
        NULL,         "w_blaster",    "w_shotgun",   "w_sshotgun",      "w_machinegun",
        "w_chaingun", "a_grenades",   "w_glauncher", "w_rlauncher",     "w_hyperblaster",
        "w_railgun",  "w_bfg",        "a_trap",      "w_ripper",        "w_phallanx",
        "a_tesla",    "w_proxlaunch", "w_chainfist", "w_disintegrator", "w_etf_rifle",
        "w_heatbeam", "w_grapple",    "w_grapple",   "w_plasma"};
    for (unsigned i = 1; i < QA_Q2_WEAPON_COUNT; ++i) {
        const qa_q2_weapon_definition *w = qa_q2_weapon_definition_at(g, (qa_q2_weapon)i);
        if (!w || g->items[i] == g->ammo[i])
            continue;
        r->definitions[r->count++] = (qa_q2_item_definition){
            .classname = w->classname,
            .name = qa_q2_weapon_display_name((qa_q2_weapon)i),
            .icon = icons[i],
            .model = w->world_model,
            .sound = QA_Q2_SOUND_MISC_W_PKUP,
            .kind = QA_Q2_ITEM_WEAPON,
            .weapon = (qa_q2_weapon)i,
            .ammo = g->ammo[i],
            .quantity = 1,
            .capacity = 32767,
            .respawn_seconds = 30,
            .rotate = true,
            .coop_stay =
                i <= QA_Q2_BFG || i == QA_Q2_LMCTF_PLASMA || g->options.rules == QA_RULESET_Q2_RERELEASE,
            .droppable = i != QA_Q2_BLASTER && i != QA_Q2_GRAPPLE && i != QA_Q2_LMCTF_HOOK,
            .console_give = i == QA_Q2_GRAPPLE || i == QA_Q2_LMCTF_HOOK ? QA_Q2_GIVE_INVENTORY_ONLY
                                                                        : QA_Q2_GIVE_PICKUP};
    }
    r->by_id = calloc(r->count, sizeof(*r->by_id));
    r->actions = calloc(r->count, sizeof(*r->actions));
    if (!r->actions || !r->by_id) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 native item actions");
        return false;
    }
    for (size_t i = 0; i < r->count; ++i) {
        qa_q2_item_definition *d = &r->definitions[i];
        if (!qa_builtin_resource(&g->services, d->classname, &d->classname_id, e))
            return false;
        bind_rules(d);
        char id[96];
        int length = snprintf(id, sizeof(id), "q2:%s", d->classname);
        if (length < 0 || (size_t)length >= sizeof(id) ||
            !qa_builtin_resource(&g->services, id, &d->item, e))
            return false;
        if (d->kind == QA_Q2_ITEM_AMMO)
            d->weapon = qa_q2_weapon_from_classname(g, d->classname);
        if (!strcmp(d->classname, "key_commander_head"))
            d->rotate = false;
        if (!strcmp(d->classname, "item_breather") || !strcmp(d->classname, "item_enviro"))
            d->coop_stay = true;
        if (!strcmp(d->classname, "ammo_tesla") ||
            (g->options.rules == QA_RULESET_Q2_RERELEASE && !strcmp(d->classname, "ammo_trap")))
            d->infinite_quantity = false;
        if (g->options.rules == QA_RULESET_Q2_RERELEASE && !strcmp(d->classname, "item_compass")) {
            d->model = "";
            d->name = "Compass";
            d->sound = "";
            d->quantity = 0;
            d->capacity = 1;
            d->coop_stay = true;
            d->rotate = false;
            d->respawn_seconds = 0;
            d->console_give = QA_Q2_GIVE_INVENTORY_ONLY;
        }
        if (!qa_builtin_resource(&g->services, d->model, &d->model_id, e) ||
            !qa_builtin_resource(&g->services, d->icon, &d->icon_id, e) ||
            !qa_builtin_resource(&g->services, d->name, &d->name_id, e) ||
            !qa_builtin_resource(&g->services, d->sound, &d->sound_id, e) ||
            !bind_localized_names(g, d, e)) return false;
        r->by_id[i]=d;
        if (d->kind == QA_Q2_ITEM_POWER && !r->powers[d->powerup]) r->powers[d->powerup]=d->item;
        if (d->kind == QA_Q2_ITEM_HEALTH || d->kind == QA_Q2_ITEM_ARMOR ||
            d->kind == QA_Q2_ITEM_SHARD || d->kind == QA_Q2_ITEM_MAX_HEALTH ||
            d->kind == QA_Q2_ITEM_PACK || d->kind == QA_Q2_ITEM_FOOD)
            continue;
        bool use = q2_item_usable(d);
        r->actions[r->action_count++] = (qa_item_definition){
            .item = d->item,
            .ammo = d->ammo,
            .owner = g->options.owner,
            .label = d->name,
            .weapon = d->weapon != QA_Q2_WEAPON_NONE,
            .actions = (use ? QA_ITEM_USE : 0) | (d->droppable ? QA_ITEM_DROP : 0)};
    }
    qsort(r->by_id,r->count,sizeof(*r->by_id),item_id_order);
    r->admissions = r->action_count ? calloc(r->action_count, sizeof(*r->admissions)) : NULL;
    if (r->action_count && !r->admissions) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 source item declarations");
        return false;
    }
    for (size_t i = 0; i < r->action_count; ++i)
        r->admissions[i].definition = r->actions[i];
    return true;
}
