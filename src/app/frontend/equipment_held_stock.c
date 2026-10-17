#include "equipment_held_stock.h"
#include "qa/scene_model_save.h"

#include <float.h>
#include <math.h>
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
    .origin = {-2.5316379070281982f, -9.270092010498047f, 4.795251846313477f},
    .axes = {{.9701912999153137f, -.16778743267059326f, -.17486034333705902f},
        {.16538193821907043f, .9858221411705017f, -.02834496460855007f},
        {.17713716626167297f, -.0014186727348715067f, .9841850996017456f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female = {
    .origin = {1.5657100677490234f, -5.886165618896484f, 1.9254425764083862f},
    .axes = {{.9919583797454834f, .036155037581920624f, -.12129008769989014f},
        {-.03602835536003113f, .9993454813957214f, .003238283796235919f},
        {.12132780998945236f, .0011576636461541057f, .992611825466156f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_male_classic = {
    .origin = {-2.198841094970703f, -9.209379196166992f, 5.1354217529296875f},
    .axes = {{.9739808440208435f, -.16668719053268433f, -.15354691445827484f},
        {.12157008051872253f, .9560694098472595f, -.26674318313598633f},
        {.19126416742801666f, .24113602936267853f, .9514575004577637f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female_classic = {
    .origin = {1.8566161394119263f, -5.973968029022217f, 2.301879405975342f},
    .axes = {{.99488365650177f, .030664639547467232f, -.09626106172800064f},
        {-.013402743265032768f, .9844618439674377f, .17508633434772491f},
        {.10013430565595627f, -.17290037870407104f, .9798359870910645f}},
    .scale = {1, 1, 1}
};
static const char *q2_carrier_name(const char *path, bool *female)
{
    static const struct { const char *path; bool female; } roots[] = {
        {"players/male/", false}, {"players/female/", true}, {"players/cyborg/", false},
        {"players/hold/male/", false}, {"players/hold/female/", true}, {"players/hold/cyborg/", false}
    };
    for (size_t i = 0; path && i < sizeof(roots) / sizeof(*roots); ++i) {
        size_t length = strlen(roots[i].path);
        if (!strncmp(path, roots[i].path, length)) {
            *female = roots[i].female;
            return path + length;
        }
    }
    return NULL;
}

static bool q2_admitted_grip(bool female, qa_model_format format, qa_ruleset_id rules,
    qa_model_transform *out)
{
    if (rules != QA_RULESET_Q2_CLASSIC && rules != QA_RULESET_Q2_RERELEASE) return false;
    if (format == QA_MODEL_MD2 && rules == QA_RULESET_Q2_CLASSIC)
        *out = female ? q2_female_classic : q2_male_classic;
    else *out = female ? q2_female : q2_male;
    return true;
}

static const char *const q2_carriers[] = {
    "weapon", "w_shotgun", "a_grenades", "a_tesla", "a_trap", "w_bfg", "w_blaster",
    "w_chainfist", "w_chaingun", "w_disrupt", "w_etfrifle", "w_glauncher", "w_grapple",
    "w_hyperblaster", "w_machinegun", "w_phalanx", "w_plasma", "w_plauncher",
    "w_railgun", "w_ripper", "w_rlauncher", "w_sshotgun"
};

static bool q2_stock_carrier(const char *name)
{
    if (!strncmp(name, "md5/", 4)) name += 4;
    const char *dot = strrchr(name, '.');
    if (!dot || (strcmp(dot, ".md2") && strcmp(dot, ".md5mesh"))) return false;
    size_t length = (size_t)(dot - name);
    for (size_t i = 0; i < sizeof(q2_carriers) / sizeof(*q2_carriers); ++i)
        if (strlen(q2_carriers[i]) == length && !memcmp(q2_carriers[i], name, length)) return true;
    return false;
}

static double triangle_area(const qa_model_vertex triangle[3])
{
    qa_vec3 origin = qa_v3(triangle[0].position[0], triangle[0].position[1], triangle[0].position[2]);
    qa_vec3 a = qa_vec_sub(qa_v3(triangle[1].position[0], triangle[1].position[1], triangle[1].position[2]), origin);
    qa_vec3 b = qa_vec_sub(qa_v3(triangle[2].position[0], triangle[2].position[1], triangle[2].position[2]), origin);
    qa_vec3 cross = qa_vec_cross(a, b);
    double area = qa_vec_dot_wide(cross, cross);
    return isfinite(area) ? area : 0;
}

/* Native Q2 carriers use the player's animation frame. Pick their socket
 * once from the actual indexed mesh; collapsed/flash-only faces lose to
 * faces present throughout more of that animation. Rendering only samples
 * the selected indices through the common model grip implementation. */
static bool q2_mesh_attachment(qa_scene_model *scene, const qa_model *model,
    const qa_model_transform *grip, qa_error *error)
{
    if (model->mesh_count != 1 || !model->frame_count) return true;
    const qa_model_mesh *mesh = model->meshes;
    const qa_model_triangle *selected = NULL;
    size_t best_frames = 0;
    double best_area = 0;
    for (uint32_t i = 0; i < mesh->triangle_count; ++i) {
        const qa_model_triangle *face = mesh->triangles + i;
        qa_model_vertex triangle[3];
        bool indexed = true;
        for (size_t j = 0; j < 3; ++j) {
            indexed &= face->vertex[j] < mesh->vertex_count;
            if (indexed) triangle[j] = mesh->vertices[face->vertex[j]];
        }
        qa_model_transform reference, inverse;
        if (!indexed || !qa_model_triangle_attachment(triangle, &reference) ||
            !qa_model_transform_inverse(&reference, &inverse)) continue;
        size_t frames = 0;
        double minimum = DBL_MAX;
        for (uint32_t frame = 0; frame < model->frame_count; ++frame) {
            for (size_t j = 0; j < 3; ++j)
                triangle[j] = mesh->vertices[(size_t)frame * mesh->vertex_count + face->vertex[j]];
            double area = triangle_area(triangle);
            if (area <= 0) continue;
            ++frames;
            if (area < minimum) minimum = area;
        }
        if (frames > best_frames || (frames == best_frames && minimum > best_area)) {
            selected = face; best_frames = frames; best_area = minimum;
        }
    }
    if (!selected || !best_frames) return true;
    return qa_scene_model_grip_bind(scene, NULL, 0, selected->vertex, grip, error);
}

bool frontend_held_stock_q2_attachment(const char *path, qa_ruleset_id rules, qa_scene_model *scene, qa_error *error)
{
    const qa_model *model = qa_scene_model_source(scene);
    if (!model || qa_scene_model_grip_defined(scene) ||
        (model->format != QA_MODEL_MD2 && model->format != QA_MODEL_MD5)) return true;
    bool female = false;
    const char *name = q2_carrier_name(path, &female);
    if (!name || !q2_stock_carrier(name)) return true;
    qa_model_transform reference;
    if (!q2_admitted_grip(female, model->format, rules, &reference)) return true;
    if (model->format == QA_MODEL_MD2) return q2_mesh_attachment(scene, model, &reference, error);
    for (uint32_t joint = 0; joint < model->bone_count; ++joint) {
        qa_bytes joint_name = qa_model_bone_name(model->bones + joint);
        if (joint_name.size != sizeof("Weapon") - 1 ||
            memcmp(joint_name.data, "Weapon", sizeof("Weapon") - 1)) continue;
        qa_model_tag tag;
        qa_model_joint_tag(model->bind_pose + joint, &tag);
        qa_model_transform socket, inverse, grip;
        qa_model_transform_identity(&socket);
        memcpy(socket.origin, tag.origin, sizeof(socket.origin));
        memcpy(socket.axes, tag.axes, sizeof(socket.axes));
        socket.scale[0] = socket.scale[1] = socket.scale[2] = model->bind_pose[joint].scale;
        if (!qa_model_transform_inverse(&socket, &inverse)) return true;
        qa_model_transform_compose(&inverse, &reference, &grip);
        return qa_scene_model_grip_bind(scene, "Weapon", 0, NULL, &grip, error);
    }
    return true;
}

bool frontend_held_stock_q2_grip(const char *path, qa_ruleset_id rules, const qa_resource *source, const qa_model *model,
    qa_model_transform *out, bool *found, qa_error *error)
{
    if (!path || !source || !model || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stock Q2 grip requires an actual admitted model resource");
        return false;
    }
    *found = false;
    bool female = false;
    const char *name = q2_carrier_name(path, &female);
    if (!name || model->format != QA_MODEL_MD2 || model->mesh_count != 1 || model->skin_count != 1)
        return true;
    if (!q2_stock_carrier(name) || !model->frame_count) return true;
    if (!q2_admitted_grip(female, model->format, rules, out)) return true;
    *found = true;
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

static bool q2(const char *view, const char *item, qa_ruleset_id rules, frontend_held_declaration *out,
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
    frontend_held_declaration result = {0};
    if (!q2_admitted_grip(false, QA_MODEL_MD2, rules, &result.grip)) return true;
    result.path = copy(path); result.fallback = copy("players/male/weapon.md2");
    if (!result.path || !result.fallback) {
        frontend_held_declaration_free(&result);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Q2 held model declaration");
        return false;
    }
    *out = result; *found = true;
    return true;
}

bool frontend_held_stock(qa_game_family family, qa_ruleset_id rules, const char *view, const char *item,
    frontend_held_declaration *out, bool *found, qa_error *error)
{
    if (!view || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original held declaration requires actual source identity");
        return false;
    }
    *found = false;
    if (family == QA_GAME_Q1) return q1(view, out, found, error);
    if (family == QA_GAME_Q2) return q2(view, item, rules, out, found, error);
    return true;
}
