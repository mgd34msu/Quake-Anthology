#include "equipment_held_stock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy(const char *text)
{
    size_t length = strlen(text) + 1;
    char *out = malloc(length);
    if (out) memcpy(out, text, length);
    return out;
}

static const qa_model_transform q1_gun = {
    .origin = {3.192499796549479f, -5.947635650634766f, 10.002536137898764f},
    .axes = {{.7190015912055969f, .6357764005661011f, -.280758261680603f},
        {-.608428418636322f, .7710461020469666f, .18789082765579224f},
        {.3359340727329254f, .03572748228907585f, .941207766532898f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q1_axe = {
    .origin = {-1.0801829099655151f, -16.939233779907227f, 3.4784603118896484f},
    .axes = {{.8529600501060486f, .34326276183128357f, .3932298421859741f},
        {-.49903303384780884f, .31537508964538574f, .8071582913398743f},
        {.15305252373218536f, -.8847085237503052f, .4403018355369568f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_male = {
    .origin = {-2.5316378672917685f, -9.27009121576945f, 4.795252025127411f},
    .axes = {{.9701912999153137f, -.16778743267059326f, -.17486034333705902f},
        {.16538193821907043f, .9858221411705017f, -.02834496460855007f},
        {.17713716626167297f, -.0014186727348715067f, .9841850996017456f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_male_classic = {
    .origin = {-2.198841094970703f, -9.209379196166992f, 5.1354217529296875f},
    .axes = {{.9739808440208435f, -.16668719053268433f, -.15354691445827484f},
        {.12157008051872253f, .9560694098472595f, -.26674318313598633f},
        {.19126416742801666f, .24113602936267853f, .9514575004577637f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female = {
    .origin = {1.5657100677490234f, -5.886165618896484f, 1.9254425764083862f},
    .axes = {{.9919583797454834f, .036155037581920624f, -.12129008769989014f},
        {-.03602835536003113f, .9993454813957214f, .003238283796235919f},
        {.12132780998945236f, .0011576636461541057f, .992611825466156f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female_classic = {
    .origin = {1.8566161394119263f, -5.973968029022217f, 2.301879405975342f},
    .axes = {{.99488365650177f, .030664639547467232f, -.09626106172800064f},
        {-.013402743265032768f, .9844618439674377f, .17508633434772491f},
        {.10013430565595627f, -.17290037870407104f, .9798359870910645f}},
    .scale = {1, 1, 1}
};

bool frontend_held_stock_q2_grip(const char *path, const qa_resource *source, const qa_model *model,
    qa_model_transform *out, bool *found, qa_error *error)
{
    if (!path || !source || !model || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stock Q2 grip requires an actual admitted model resource");
        return false;
    }
    *found = false;
    const char *name = NULL;
    bool female = false;
    static const struct { const char *path; bool female; } roots[] = {
        {"players/male/", false}, {"players/female/", true}, {"players/cyborg/", false},
        {"players/hold/male/", false}, {"players/hold/female/", true}, {"players/hold/cyborg/", false}
    };
    for (size_t i = 0; path && i < sizeof(roots) / sizeof(*roots); ++i) {
        size_t length = strlen(roots[i].path);
        if (!strncmp(path, roots[i].path, length)) {
            name = path + length; female = roots[i].female; break;
        }
    }
    if (!name || model->format != QA_MODEL_MD2 || model->mesh_count != 1 || model->skin_count != 1)
        return true;
    const qa_model_mesh *mesh = model->meshes;
    uint64_t bytes = qa_resource_bytes(source).size;
    if ((!strcmp(name, "weapon.md2") || !strcmp(name, "w_shotgun.md2")) && bytes == 38912 &&
        model->frame_count == 173 && model->skin_width == 136 && model->skin_height == 60 &&
        mesh->vertex_count == 42 && mesh->texcoord_count == 70 && mesh->triangle_count == 72) {
        *out = female ? q2_female_classic : q2_male_classic;
        *found = true; return true;
    }
    static const struct {
        const char *name;
        uint32_t width, height, vertices, texcoords, triangles;
        uint64_t frames173_bytes, frames174_bytes, female173_bytes;
    } layouts[] = {
        {"weapon.md2", 184,132,140,138,148,109272,0,109244},
        {"w_shotgun.md2", 184,132,140,138,148,109272,0,109244},
        {"a_grenades.md2", 72,36,44,44,48,39252,0,0},
        {"a_grenades.md2", 168,84,32,46,60,0,31329,0},
        {"a_tesla.md2", 296,194,216,216,252,165204,0,165260},
        {"a_trap.md2", 264,184,330,309,200,243888,0,0},
        {"w_bfg.md2", 308,165,118,199,192,96244,96757,0},
        {"w_blaster.md2", 312,183,86,171,144,72136,72520,0},
        {"w_chainfist.md2", 320,200,193,288,266,150640,151453,0},
        {"w_chaingun.md2", 308,168,74,122,124,63072,63409,0},
        {"w_disrupt.md2", 280,194,199,259,270,154352,155189,0},
        {"w_etfrifle.md2", 240,194,133,210,218,107320,107893,0},
        {"w_glauncher.md2", 308,176,111,181,198,91080,0,0},
        {"w_glauncher.md2", 308,176,97,175,178,0,81120,0},
        {"w_grapple.md2", 304,194,73,115,130,62384,62717,0},
        {"w_hyperblaster.md2", 292,194,85,144,142,71232,71613,0},
        {"w_machinegun.md2", 312,106,77,128,134,65104,65453,0},
        {"w_phalanx.md2", 240,200,161,259,200,126740,127481,0},
        {"w_plasma.md2", 272,195,124,210,208,100684,101221,0},
        {"w_plauncher.md2", 320,200,111,181,198,91080,91565,0},
        {"w_railgun.md2", 292,194,112,188,192,91684,92173,0},
        {"w_ripper.md2", 304,200,110,287,191,91344,91881,0},
        {"w_rlauncher.md2", 284,194,119,186,202,96704,97221,0},
        {"w_sshotgun.md2", 308,169,103,188,182,85104,85556,0}
    };
    for (size_t i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i) {
        uint64_t expected = model->frame_count == 173 ?
            (female && layouts[i].female173_bytes ? layouts[i].female173_bytes : layouts[i].frames173_bytes) :
            model->frame_count == 174 ? layouts[i].frames174_bytes : 0;
        if (!expected || bytes != expected || strcmp(name, layouts[i].name) ||
            model->skin_width != layouts[i].width || model->skin_height != layouts[i].height ||
            mesh->vertex_count != layouts[i].vertices || mesh->texcoord_count != layouts[i].texcoords ||
            mesh->triangle_count != layouts[i].triangles) continue;
        *out = female ? q2_female : q2_male;
        if (!female) {
            out->origin[0] = -2.5316379070281982f; out->origin[1] = -9.270092010498047f;
            out->origin[2] = 4.795251846313477f;
        }
        *found = true; return true;
    }
    return true;
}

static bool q1(const char *view, frontend_held_declaration *out, bool *found, qa_error *error)
{
    size_t length = strlen(view);
    if (length < 12 || strncmp(view, "progs/v_", 8) || strcmp(view + length - 4, ".mdl")) return true;
    static const uint32_t gun[] = {40,41,42,100,101,102,116,117,118,142,143,144,145,146,
        147,148,156,157,158,174,175,177,178,179,185,186,187,188,189,190,191,194,195,196,199,200,201,202};
    static const uint32_t axe[] = {9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24};
    bool ax = !strcmp(view, "progs/v_axe.mdl");
    const uint32_t *vertices = ax ? axe : gun;
    frontend_held_declaration result = {.reference_frame = ax ? 17 : 12,
        .vertex_count = ax ? sizeof(axe) / sizeof(*axe) : sizeof(gun) / sizeof(*gun),
        .grip = ax ? q1_axe : q1_gun};
    result.path = copy("progs/player.mdl");
    result.vertices = malloc(result.vertex_count * sizeof(*result.vertices));
    if (!result.path || !result.vertices) {
        frontend_held_declaration_free(&result);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Q1 held model declaration");
        return false;
    }
    memcpy(result.vertices, vertices, result.vertex_count * sizeof(*result.vertices));
    *out = result; *found = true;
    return true;
}

static bool q2(const char *view, const char *item, frontend_held_declaration *out,
    bool *found, qa_error *error)
{
    static const struct { const char *item, *model; } models[] = {
        {"q2:weapon_blaster", "w_blaster"}, {"q2:weapon_shotgun", "w_shotgun"},
        {"q2:weapon_supershotgun", "w_sshotgun"}, {"q2:weapon_machinegun", "w_machinegun"},
        {"q2:weapon_chaingun", "w_chaingun"}, {"q2:ammo_grenades", "a_grenades"},
        {"q2:weapon_grenadelauncher", "w_glauncher"}, {"q2:weapon_rocketlauncher", "w_rlauncher"},
        {"q2:weapon_hyperblaster", "w_hyperblaster"}, {"q2:weapon_railgun", "w_railgun"},
        {"q2:weapon_bfg", "w_bfg"}, {"q2:ammo_trap", "a_trap"},
        {"q2:weapon_boomer", "w_ripper"}, {"q2:weapon_phalanx", "w_phalanx"},
        {"q2:ammo_tesla", "a_tesla"}, {"q2:weapon_proxlauncher", "w_plauncher"},
        {"q2:weapon_chainfist", "w_chainfist"}, {"q2:weapon_disintegrator", "w_disrupt"},
        {"q2:weapon_etf_rifle", "w_etfrifle"}, {"q2:weapon_plasmabeam", "w_plasma"}
    };
    const char *model = NULL;
    if (item) for (size_t i = 0; i < sizeof(models) / sizeof(*models); ++i)
        if (!strcmp(item, models[i].item)) { model = models[i].model; break; }
    if (!item) {
        static const char *const views[] = {
            "models/weapons/v_blast/tris.md2", "models/weapons/v_shotg/tris.md2",
            "models/weapons/v_shotg2/tris.md2", "models/weapons/v_machn/tris.md2",
            "models/weapons/v_chain/tris.md2", "models/weapons/v_handgr/tris.md2",
            "models/weapons/v_launch/tris.md2", "models/weapons/v_rocket/tris.md2",
            "models/weapons/v_hyperb/tris.md2", "models/weapons/v_rail/tris.md2",
            "models/weapons/v_bfg/tris.md2", "models/weapons/v_trap/tris.md2",
            "models/weapons/v_boomer/tris.md2", "models/weapons/v_shotx/tris.md2",
            "models/weapons/v_tesla/tris.md2", "models/weapons/v_launch/tris.md2",
            "models/weapons/v_chainf/tris.md2", "models/weapons/v_dist/tris.md2",
            "models/weapons/v_etf_rifle/tris.md2", "models/weapons/v_beamer/tris.md2"
        };
        for (size_t i = 0; i < sizeof(views) / sizeof(*views); ++i)
            if (!strcmp(view, views[i])) { model = models[i].model; break; }
    }
    if (!model && !strcmp(view, "models/weapons/grapple/tris.md2")) model = "w_grapple";
    if (!model) return true;
    char path[96];
    snprintf(path, sizeof(path), "players/male/%s.md2", model);
    frontend_held_declaration result = {.grip = q2_male};
    result.path = copy(path); result.fallback = copy("players/male/weapon.md2");
    if (!result.path || !result.fallback) {
        frontend_held_declaration_free(&result);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Q2 held model declaration");
        return false;
    }
    *out = result; *found = true;
    return true;
}

bool frontend_held_stock(qa_game_family family, const char *view, const char *item,
    frontend_held_declaration *out, bool *found, qa_error *error)
{
    if (!view || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original held declaration requires actual source identity");
        return false;
    }
    *found = false;
    if (family == QA_GAME_Q1) return q1(view, out, found, error);
    if (family == QA_GAME_Q2) return q2(view, item, out, found, error);
    return true;
}
