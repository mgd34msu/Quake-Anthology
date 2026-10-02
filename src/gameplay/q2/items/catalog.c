#include "internal.h"
#include <ctype.h>
#include <stdio.h>

#define VIS(c, n, m, i, s, r)                                                                      \
    .classname = c, .name = n, .model = m, .icon = i, .sound = s, .respawn_seconds = r
#define AMMO(c, n, m, i, q, cap, rot)                                                              \
    {VIS(c, n, m, i, "misc/am_pkup.wav", 30),                                                      \
     .kind = QA_Q2_ITEM_AMMO,                                                                      \
     .quantity = q,                                                                                \
     .capacity = cap,                                                                              \
     .droppable = true,                                                                            \
     .rotate = rot,                                                                                \
     .infinite_quantity = true}
#define KEY(c, n, m, i)                                                                            \
    {VIS(c, n, m, i, "items/pkup.wav", 0),                                                         \
     .kind = QA_Q2_ITEM_KEY,                                                                       \
     .quantity = 1,                                                                                \
     .capacity = 32767,                                                                            \
     .coop_stay = true,                                                                            \
     .droppable = true,                                                                            \
     .rotate = true}
#define POWER(c, n, m, i, t, p)                                                                    \
    {VIS(c, n, m, i, "items/pkup.wav", t),                                                         \
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
    {VIS(c, n, m, i, "misc/ar1_pkup.wav", 20),                                                     \
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
         "items/pkup.wav", 60),
     .kind = QA_Q2_ITEM_PACK, .rotate = true},
    {VIS("item_pack", "Ammo Pack", "models/items/pack/tris.md2", "i_pack", "items/pkup.wav", 180),
     .kind = QA_Q2_ITEM_PACK, .rotate = true, .full_pack = true},
    HEALTH("item_health", "models/items/healing/medium/tris.md2", "items/n_health.wav", 10, false,
           false),
    HEALTH("item_health_small", "models/items/healing/stimpack/tris.md2", "items/s_health.wav", 2,
           true, false),
    HEALTH("item_health_large", "models/items/healing/large/tris.md2", "items/l_health.wav", 25,
           false, false),
    HEALTH("item_health_mega", "models/items/mega_h/tris.md2", "items/m_health.wav", 100, true,
           true),
    ARMOR("item_armor_jacket", "Jacket Armor", "models/items/armor/jacket/tris.md2",
          "i_jacketarmor", 25, 50, .3f, 0),
    ARMOR("item_armor_combat", "Combat Armor", "models/items/armor/combat/tris.md2",
          "i_combatarmor", 50, 100, .6f, .3f),
    ARMOR("item_armor_body", "Body Armor", "models/items/armor/body/tris.md2", "i_bodyarmor", 100,
          200, .8f, .6f),
    {VIS("item_armor_shard", "Armor Shard", "models/items/armor/shard/tris.md2", "i_jacketarmor",
         "misc/ar2_pkup.wav", 20),
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
         "misc/ar3_pkup.wav", 60),
     .kind = QA_Q2_ITEM_POWER_ARMOR, .powered_armor = QA_POWER_SCREEN, .capacity = 32767,
     .quantity = 1, .droppable = true, .rotate = true},
    {VIS("item_power_shield", "Power Shield", "models/items/armor/shield/tris.md2", "i_powershield",
         "misc/ar3_pkup.wav", 60),
     .kind = QA_Q2_ITEM_POWER_ARMOR, .powered_armor = QA_POWER_SHIELD, .capacity = 32767,
     .quantity = 1, .droppable = true, .rotate = true},
    {VIS("item_adrenaline", "Adrenaline", "models/items/adrenal/tris.md2", "p_adrenaline",
         "items/pkup.wav", 60),
     .kind = QA_Q2_ITEM_MAX_HEALTH, .quantity = 1, .fill = true, .rotate = true},
    {VIS("item_ancient_head", "Ancient Head", "models/items/c_head/tris.md2", "i_fixme",
         "items/pkup.wav", 60),
     .kind = QA_Q2_ITEM_MAX_HEALTH, .quantity = 2, .rotate = true}};
static const qa_q2_item_definition xatrix[] = {
    AMMO("ammo_magslug", "Mag Slug", "models/objects/ammo/tris.md2", "a_mslugs", 10, 50, false),
    AMMO("ammo_trap", "Trap", "models/weapons/g_trap/tris.md2", "a_trap", 1, 5, true),
    KEY("key_green_key", "Green Key", "models/items/keys/green_key/tris.md2", "k_green"),
    POWER("item_quadfire", "DualFire Damage", "models/items/quadfire/tris.md2", "p_quadfire", 60,
          QA_Q2_POWER_QUADFIRE),
    {VIS("item_foodcube", "Health", "models/objects/trapfx/tris.md2", "i_health",
         "items/s_health.wav", 0),
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
         "items/pkup.wav", 90),
     .kind = QA_Q2_ITEM_DECOY, .quantity = 1, .capacity = 1, .droppable = true, .rotate = true},
    {VIS("ammo_nuke", "A-M Bomb", "models/weapons/g_nuke/tris.md2", "p_nuke", "misc/am_pkup.wav",
         300),
     .kind = QA_Q2_ITEM_NUKE, .quantity = 1, .capacity = 1, .droppable = true, .rotate = true},
    {VIS("item_sphere_defender", "defender sphere", "models/items/defender/tris.md2", "p_defender",
         "items/pkup.wav", 60),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_sphere_hunter", "hunter sphere", "models/items/hunter/tris.md2", "p_hunter",
         "items/pkup.wav", 120),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_sphere_vengeance", "vengeance sphere", "models/items/vengnce/tris.md2",
         "p_vengeance", "items/pkup.wav", 60),
     .kind = QA_Q2_ITEM_SPHERE, .quantity = 1, .capacity = 32767, .rotate = true},
    {VIS("item_compass", "compass", "models/objects/fire/tris.md2", "p_compass", "items/pkup.wav",
         60),
     .kind = QA_Q2_ITEM_COMPASS, .quantity = 1, .capacity = 32767, .rotate = true}};
static const qa_q2_item_definition rerelease[] = {
    POWER("item_invisibility", "Invisibility", "models/items/cloaker/tris.md2", "p_cloaker", 300,
          QA_Q2_POWER_INVISIBILITY),
    {VIS("item_flashlight", "Flashlight", "models/items/flashlight/tris.md2", "p_torch",
         "items/pkup.wav", 0),
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
const qa_q2_item_definition *q2_item_by_id(qa_q2_game *g, qa_item_id id) {
    for (size_t i = 0; i < qa_q2_item_count(g); ++i)
        if (g->item_runtime->definitions[i].item == id)
            return &g->item_runtime->definitions[i];
    return NULL;
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
    if (g->options.product == QA_Q2_XATRIX || g->options.edition == QA_Q2_RERELEASE)
        install(r, xatrix, N(xatrix));
    if (g->options.product == QA_Q2_ROGUE || g->options.edition == QA_Q2_RERELEASE)
        install(r, rogue, N(rogue));
    if (g->options.edition == QA_Q2_RERELEASE)
        install(r, rerelease, N(rerelease));
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
            .sound = "misc/w_pkup.wav",
            .kind = QA_Q2_ITEM_WEAPON,
            .weapon = (qa_q2_weapon)i,
            .ammo = g->ammo[i],
            .quantity = 1,
            .capacity = 32767,
            .respawn_seconds = 30,
            .rotate = true,
            .coop_stay =
                i <= QA_Q2_BFG || i == QA_Q2_LMCTF_PLASMA || g->options.edition == QA_Q2_RERELEASE,
            .droppable = i != QA_Q2_BLASTER && i != QA_Q2_GRAPPLE && i != QA_Q2_LMCTF_HOOK,
            .console_give = i == QA_Q2_GRAPPLE || i == QA_Q2_LMCTF_HOOK ? QA_Q2_GIVE_INVENTORY_ONLY
                                                                        : QA_Q2_GIVE_PICKUP};
    }
    r->actions = calloc(r->count, sizeof(*r->actions));
    if (!r->actions) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 native item actions");
        return false;
    }
    for (size_t i = 0; i < r->count; ++i) {
        qa_q2_item_definition *d = &r->definitions[i];
        if (!qa_builtin_resource(&g->services, d->classname, &d->classname_id, e))
            return false;
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
            (g->options.edition == QA_Q2_RERELEASE && !strcmp(d->classname, "ammo_trap")))
            d->infinite_quantity = false;
        if (g->options.edition == QA_Q2_RERELEASE && !strcmp(d->classname, "item_compass")) {
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
        if (d->kind == QA_Q2_ITEM_HEALTH || d->kind == QA_Q2_ITEM_ARMOR ||
            d->kind == QA_Q2_ITEM_SHARD || d->kind == QA_Q2_ITEM_MAX_HEALTH ||
            d->kind == QA_Q2_ITEM_PACK || d->kind == QA_Q2_ITEM_FOOD)
            continue;
        bool use = d->weapon != QA_Q2_WEAPON_NONE || d->kind == QA_Q2_ITEM_POWER || d->kind == QA_Q2_ITEM_POWER_ARMOR ||
                   d->kind == QA_Q2_ITEM_SPHERE || d->kind == QA_Q2_ITEM_DECOY ||
                   d->kind == QA_Q2_ITEM_NUKE || d->kind == QA_Q2_ITEM_COMPASS ||
                   d->kind == QA_Q2_ITEM_FLASHLIGHT;
        r->actions[r->action_count++] = (qa_item_definition){
            .item = d->item,
            .ammo = d->ammo,
            .owner = g->options.owner,
            .label = d->name,
            .weapon = d->weapon != QA_Q2_WEAPON_NONE,
            .actions = (use ? QA_ITEM_USE : 0) | (d->droppable ? QA_ITEM_DROP : 0)};
    }
    r->admissions = r->action_count ? calloc(r->action_count, sizeof(*r->admissions)) : NULL;
    if (r->action_count && !r->admissions) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 source item declarations");
        return false;
    }
    for (size_t i = 0; i < r->action_count; ++i)
        r->admissions[i].definition = r->actions[i];
    return true;
}
