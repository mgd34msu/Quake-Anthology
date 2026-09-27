#include "internal.h"

#define B(n) (UINT64_C(1) << (n))
#define D(id, n, i, a, q, w, v, g, p, ac, f, idle, de, pa, fi, r)                                  \
    {id, n, "q2:" i, i, a, "models/" v, "models/" g, q, w, p, ac, f, idle, de, pa, fi, r}
static const qa_q2_weapon_definition base[] = {
    D(QA_Q2_BLASTER, "blaster", "weapon_blaster", NULL, 0, 0, "weapons/v_blast/tris.md2", "", 1, 4,
      8, 52, 55, B(19) | B(32), B(5), false),
    D(QA_Q2_SHOTGUN, "shotgun", "weapon_shotgun", "q2:ammo_shells", 1, 5,
      "weapons/v_shotg/tris.md2", "weapons/g_shotg/tris.md2", 2, 7, 18, 36, 39,
      B(22) | B(28) | B(34), B(8) | B(9), false),
    D(QA_Q2_SUPERSHOTGUN, "supershotgun", "weapon_supershotgun", "q2:ammo_shells", 2, 10,
      "weapons/v_shotg2/tris.md2", "weapons/g_shotg2/tris.md2", 3, 6, 17, 57, 61,
      B(29) | B(42) | B(57), B(7), false),
    D(QA_Q2_MACHINEGUN, "machinegun", "weapon_machinegun", "q2:ammo_bullets", 1, 30,
      "weapons/v_machn/tris.md2", "weapons/g_machn/tris.md2", 4, 3, 5, 45, 49, B(23) | B(45),
      B(4) | B(5), true),
    D(QA_Q2_CHAINGUN, "chaingun", "weapon_chaingun", "q2:ammo_bullets", 1, 60,
      "weapons/v_chain/tris.md2", "weapons/g_chain/tris.md2", 5, 4, 31, 61, 64,
      B(38) | B(43) | B(51) | B(61), (B(22) - 1) & ~(B(5) - 1), true),
    D(QA_Q2_GRENADES, "grenades", "ammo_grenades", "q2:ammo_grenades", 1, 2,
      "weapons/v_handgr/tris.md2", "items/ammo/grenades/medium/tris.md2", 6, 0, 15, 48, 48,
      B(29) | B(34) | B(39) | B(48), B(12), false),
    D(QA_Q2_GRENADELAUNCHER, "grenadelauncher", "weapon_grenadelauncher", "q2:ammo_grenades", 1, 5,
      "weapons/v_launch/tris.md2", "weapons/g_launch/tris.md2", 7, 5, 16, 59, 64,
      B(34) | B(51) | B(59), B(6), false),
    D(QA_Q2_ROCKETLAUNCHER, "rocketlauncher", "weapon_rocketlauncher", "q2:ammo_rockets", 1, 5,
      "weapons/v_rocket/tris.md2", "weapons/g_rocket/tris.md2", 8, 4, 12, 50, 54,
      B(25) | B(33) | B(42) | B(50), B(5), false),
    D(QA_Q2_HYPERBLASTER, "hyperblaster", "weapon_hyperblaster", "q2:ammo_cells", 1, 30,
      "weapons/v_hyperb/tris.md2", "weapons/g_hyperb/tris.md2", 9, 5, 20, 49, 53, 0,
      B(6) | B(7) | B(8) | B(9) | B(10) | B(11), true),
    D(QA_Q2_RAILGUN, "railgun", "weapon_railgun", "q2:ammo_slugs", 1, 5, "weapons/v_rail/tris.md2",
      "weapons/g_rail/tris.md2", 10, 3, 18, 56, 61, B(56), B(4), false),
    D(QA_Q2_BFG, "bfg", "weapon_bfg", "q2:ammo_cells", 50, 50, "weapons/v_bfg/tris.md2",
      "weapons/g_bfg/tris.md2", 11, 8, 32, 55, 58, B(39) | B(45) | B(50) | B(55), B(9) | B(17),
      false),
};
static const qa_q2_weapon_definition xatrix[] = {
    D(QA_Q2_TRAP, "trap", "ammo_trap", "q2:ammo_trap", 1, 1, "weapons/v_trap/tris.md2",
      "weapons/g_trap/tris.md2", 0, 0, 15, 48, 48, B(29) | B(34) | B(39) | B(48), B(12), false),
    D(QA_Q2_IONRIPPER, "ionripper", "weapon_boomer", "q2:ammo_cells", 2, 10,
      "weapons/v_boomer/tris.md2", "weapons/g_boom/tris.md2", 13, 4, 6, 36, 39, B(36), B(5), false),
    D(QA_Q2_PHALANX, "phalanx", "weapon_phalanx", "q2:ammo_magslug", 1, 5,
      "weapons/v_shotx/tris.md2", "weapons/g_shotx/tris.md2", 12, 5, 20, 58, 63,
      B(29) | B(42) | B(55), B(7) | B(8), false),
};
static const qa_q2_weapon_definition rogue[] = {
    D(QA_Q2_TESLA, "tesla", "ammo_tesla", "q2:ammo_tesla", 1, 2, "weapons/v_tesla/tris.md2",
      "ammo/am_tesl/tris.md2", 0, 0, 8, 32, 32, B(21), B(2), false),
    D(QA_Q2_PROXLAUNCHER, "proxlauncher", "weapon_proxlauncher", "q2:ammo_prox", 1, 5,
      "weapons/v_launch/tris.md2", "weapons/g_launch/tris.md2", 15, 5, 16, 59, 64,
      B(34) | B(51) | B(59), B(6), false),
    D(QA_Q2_CHAINFIST, "chainfist", "weapon_chainfist", NULL, 0, 0, "weapons/v_chainf/tris.md2",
      "weapons/g_chainf/tris.md2", 16, 4, 32, 57, 60, 0,
      B(8) | B(9) | B(16) | B(17) | B(18) | B(30) | B(31), false),
    D(QA_Q2_DISINTEGRATOR, "disintegrator", "weapon_disintegrator", "q2:ammo_disruptor", 1, 5,
      "weapons/v_dist/tris.md2", "weapons/g_dist/tris.md2", 12, 4, 9, 29, 34, B(14) | B(19) | B(23),
      B(5), false),
    D(QA_Q2_ETF_RIFLE, "etf_rifle", "weapon_etf_rifle", "q2:ammo_flechettes", 1, 30,
      "weapons/v_etf_rifle/tris.md2", "weapons/g_etf_rifle/tris.md2", 13, 4, 7, 37, 41,
      B(18) | B(28), B(6) | B(7), false),
    D(QA_Q2_HEATBEAM, "heatbeam", "weapon_plasmabeam", "q2:ammo_cells", 2, 10,
      "weapons/v_beamer/tris.md2", "weapons/g_beamer/tris.md2", 14, 8, 12, 39, 44, B(35),
      B(9) | B(10) | B(11) | B(12), false),
};
static const qa_q2_weapon_definition grapples[] = {
    D(QA_Q2_GRAPPLE, "grapple", "weapon_grapple", NULL, 0, 0, "weapons/grapple/tris.md2", "", 12, 5,
      9, 31, 36, B(10) | B(18) | B(27), B(6), false),
    D(QA_Q2_LMCTF_HOOK, "lmctf:hook", "weapon_hook", NULL, 0, 0, "weapons/v_hook/tris.md2",
      "objects/debris2/tris.md2", 11, 9, 13, 34, 38, B(14) | B(18) | B(26) | B(30),
      B(8) | B(9) | B(10) | B(11), true),
    D(QA_Q2_LMCTF_PLASMA, "lmctf:plasma", "weapon_plasma", "q2:ammo_cells", 10, 10,
      "weapons/v_plasma/tris.md2", "weapons/g_plasma/tris.md2", 12, 3, 11, 46, 51, B(16) | B(46),
      B(4) | B(5), false),
};
#undef D
static void install(qa_q2_game *g, const qa_q2_weapon_definition *d, size_t count) {
    for (size_t i = 0; i < count; ++i)
        g->definitions[d[i].weapon] = d[i];
}
bool q2_definitions(qa_q2_game *g, qa_error *error) {
    install(g, base, sizeof(base) / sizeof(*base));
    install(g, grapples, sizeof(grapples) / sizeof(*grapples));
    g->definitions[QA_Q2_BLASTER].world_model = "";
    g->definitions[QA_Q2_GRAPPLE].world_model = "";
    if (g->options.product == QA_Q2_XATRIX || g->options.edition == QA_Q2_RERELEASE)
        install(g, xatrix, sizeof(xatrix) / sizeof(*xatrix));
    if (g->options.product == QA_Q2_ROGUE || g->options.edition == QA_Q2_RERELEASE)
        install(g, rogue, sizeof(rogue) / sizeof(*rogue));
    if (g->options.edition == QA_Q2_RERELEASE) {
        g->definitions[QA_Q2_GRAPPLE].fire_last = 10;
        g->definitions[QA_Q2_IONRIPPER].activate_last = 5;
        g->definitions[QA_Q2_IONRIPPER].fire_last = 7;
        g->definitions[QA_Q2_IONRIPPER].fires = B(6);
        g->definitions[QA_Q2_HEATBEAM].repeating = true;
        g->definitions[QA_Q2_HEATBEAM].idle_last = 42;
        g->definitions[QA_Q2_HEATBEAM].deactivate_last = 47;
        g->definitions[QA_Q2_CHAINFIST].repeating = true;
        g->definitions[QA_Q2_ETF_RIFLE].repeating = true;
    }
    for (size_t i = 1; i < QA_Q2_WEAPON_COUNT; ++i) {
        const qa_q2_weapon_definition *d = &g->definitions[i];
        if (d->name == NULL)
            continue;
        if (!qa_builtin_resource(&g->services, d->item, &g->items[i], error) ||
            !qa_builtin_resource(&g->services, d->view_model, &g->view_models[i], error) ||
            (d->ammo != NULL && !qa_builtin_resource(&g->services, d->ammo, &g->ammo[i], error)))
            return false;
    }
    return true;
}
const qa_q2_weapon_definition *qa_q2_weapon_definition_at(const qa_q2_game *g,
                                                          qa_q2_weapon weapon) {
    return g != NULL && weapon > QA_Q2_WEAPON_NONE && weapon < QA_Q2_WEAPON_COUNT &&
                   g->definitions[weapon].name != NULL
               ? &g->definitions[weapon]
               : NULL;
}
qa_q2_weapon qa_q2_weapon_from_classname(const qa_q2_game *g, const char *classname) {
    if (g != NULL && classname != NULL)
        for (size_t i = 1; i < QA_Q2_WEAPON_COUNT; ++i)
            if (g->definitions[i].classname != NULL &&
                strcmp(g->definitions[i].classname, classname) == 0)
                return (qa_q2_weapon)i;
    return QA_Q2_WEAPON_NONE;
}
