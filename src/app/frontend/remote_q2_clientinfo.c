#include "remote_q2_clientinfo.h"
#include "remote_q2_private.h"
#include "qa/material.h"
#include <stdio.h>
#include <string.h>

static bool component(const char *text)
{
    if (!*text || !strcmp(text, ".") || !strcmp(text, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("_+.-", *p))) return false;
    return true;
}
static bool equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static void appearance(frontend_remote_q2 *row, const char *text, char model[64], char skin[64])
{
    const char *start = strchr(text, '\\'); start = start ? start + 1 : text;
    const char *slash = strchr(start, '/');
    if (!slash && !remote_q2_rerelease_presentation(row)) slash = strchr(start, '\\');
    const qa_cvar_view *setting = qa_cvars_find(row->options.domain.cvars, "cl_noskins");
    double noskins = setting ? setting->number : 0;
    size_t length = slash ? (size_t)(slash - start) : 0;
    if (!length || length >= 64) strcpy(model, "male");
    else { memcpy(model, start, length); model[length] = 0; }
    const char *value = slash ? slash + 1 : "";
    const char *end = strchr(value, '\\'); length = end ? (size_t)(end - value) : strlen(value);
    if (length >= 64) length = 0;
    memcpy(skin, value, length); skin[length] = 0;
    if (remote_q2_rerelease_presentation(row)) {
        if (noskins == 2 || !component(skin)) {
            if (equal(model, "female")) { strcpy(model, "female"); strcpy(skin, "athena"); }
            else { strcpy(model, "male"); strcpy(skin, "grunt"); }
        } else if (noskins != 0 || !component(model)) { strcpy(model, "male"); strcpy(skin, "grunt"); }
    } else if (noskins != 0 || !component(model) || !component(skin)) {
        strcpy(model, "male"); strcpy(skin, "grunt");
    }
}
static bool model_admit(frontend_remote_q2 *row, const char *path, bool *present, qa_error *error)
{
    remote_q2_model *held = NULL; qa_error issue = {0}; *present = false;
    if (remote_q2_model_read(row, path, &held, &issue)) { *present = true; return true; }
    if (issue.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = issue;
    return false;
}
static bool skin_admit(frontend_remote_q2 *row, const char *path, bool *present, qa_error *error)
{
    qa_scene_image_options options = {.family = QA_SCENE_Q2, .usage = QA_IMAGE_USAGE_SKIN,
        .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
        .transparent = true, .transparent_index = 255};
    const qa_material *material = NULL;
    if (!qa_material_register(row->materials, path, &options, false, &material, error)) return false;
    *present = material && !material->default_shader; return true;
}
static const char *weapon_name(frontend_remote_q2 *row, uint32_t weapon)
{
    if (!weapon) return "weapon.md2";
    uint32_t count = 0;
    for (size_t i = 1; i < row->layout.max_models && count < 31; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + i));
        if (name[0] == '#' && ++count == weapon) return name + 1;
    }
    return NULL;
}
static bool load(frontend_remote_q2 *row, const char *text, uint32_t weapon,
    remote_q2_clientinfo *out, bool base, qa_error *error)
{
    char model[64], skin[64]; appearance(row, text, model, skin);
    bool has_model = false, has_skin = false, has_weapon = false;
    snprintf(out->model, sizeof(out->model), "players/%s/tris.md2", model);
    if (!model_admit(row, out->model, &has_model, error)) return false;
    if (!has_model && !equal(model, "male")) {
        strcpy(model, "male"); strcpy(out->model, "players/male/tris.md2");
        if (!model_admit(row, out->model, &has_model, error)) return false;
    }
    snprintf(out->skin, sizeof(out->skin), "players/%s/%s.pcx", model, skin);
    if (!skin_admit(row, out->skin, &has_skin, error)) return false;
    if (!has_skin && remote_q2_rerelease_presentation(row) && equal(model, "female")) {
        strcpy(skin, "athena"); strcpy(out->skin, "players/female/athena.pcx");
        if (!skin_admit(row, out->skin, &has_skin, error)) return false;
    }
    if (!has_skin && !equal(model, "male")) {
        strcpy(model, "male"); strcpy(out->model, "players/male/tris.md2");
        snprintf(out->skin, sizeof(out->skin), "players/male/%s.pcx", skin);
        if (!model_admit(row, out->model, &has_model, error) || !skin_admit(row, out->skin, &has_skin, error)) return false;
    }
    if (!has_skin) {
        snprintf(out->skin, sizeof(out->skin), "players/%s/grunt.pcx", model);
        if (remote_q2_rerelease_presentation(row)) strcpy(skin, "grunt");
        if (!skin_admit(row, out->skin, &has_skin, error)) return false;
    }
    const qa_cvar_view *vwep = qa_cvars_find(row->options.domain.cvars, "cl_vwep");
    if ((vwep && vwep->number == 0) || weapon >= 32) weapon = 0;
    const char *name = weapon_name(row, weapon);
    if (name && *name) {
        if (strlen(name) > 127) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 player weapon exceeds its native client-info path extent");
        snprintf(out->weapon, sizeof(out->weapon), "players/%s/%s", model, name);
        if (!model_admit(row, out->weapon, &has_weapon, error)) return false;
        if (!has_weapon && equal(model, "cyborg")) {
            snprintf(out->weapon, sizeof(out->weapon), "players/male/%s", name);
            if (!model_admit(row, out->weapon, &has_weapon, error)) return false;
        }
    }
    if (!has_weapon && weapon) {
        snprintf(out->weapon, sizeof(out->weapon), "players/%s/weapon.md2", model);
        if (!model_admit(row, out->weapon, &has_weapon, error)) return false;
    }
    if (!has_weapon) *out->weapon = 0;
    char icon[256]; snprintf(icon, sizeof(icon), "/players/%s/%s_i.pcx", model, skin);
    qa_error issue = {0}; const qa_scene_image *image = remote_q2_picture_read(row, icon, &issue);
    if (!image && issue.code != QA_OK && issue.code != QA_ERROR_NOT_FOUND) { if (error) *error = issue; return false; }
    bool default_weapon = false; char default_path[256];
    snprintf(default_path, sizeof(default_path), "players/%s/weapon.md2", model);
    if (!model_admit(row, default_path, &default_weapon, error)) return false;
    out->valid = has_model && has_skin && default_weapon && (remote_q2_rerelease_presentation(row) || image);
    if (!out->valid && !base) return load(row, "male/grunt", 0, out, true, error);
    if (!has_weapon && !base) {
        bool fallback = false; strcpy(out->weapon, "players/male/weapon.md2");
        if (!model_admit(row, out->weapon, &fallback, error)) return false;
        if (!fallback) *out->weapon = 0;
    }
    return true;
}
bool remote_q2_clientinfo_read(frontend_remote_q2 *row, uint32_t player, uint32_t weapon,
    remote_q2_clientinfo *out, qa_error *error)
{
    if (!row || !out || !row->materials || player > 256) return false;
    *out = (remote_q2_clientinfo){0};
    return load(row, player == 256 ? "male/grunt" : frontend_remote_q2_config(row,
        (uint16_t)(row->layout.players + player)), weapon, out, player == 256, error);
}
bool remote_q2_clientinfo_prepare(frontend_remote_q2 *row, qa_error *error)
{
    remote_q2_clientinfo info;
    if (!remote_q2_clientinfo_read(row, 256, 0, &info, error)) return false;
    for (uint32_t i = 0; i < 256; ++i) {
        if (!*frontend_remote_q2_config(row, (uint16_t)(row->layout.players + i))) continue;
        for (uint32_t weapon = 0; weapon < 32 && weapon_name(row, weapon); ++weapon)
            if (!remote_q2_clientinfo_read(row, i, weapon, &info, error)) return false;
    }
    return true;
}
