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
    const qa_cvar_view *setting = qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_noskins);
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
static bool model_admit(frontend_remote_q2 *row, const char *path, remote_q2_model **out, qa_error *error)
{
    qa_error issue = {0}; *out = NULL;
    if (remote_q2_model_read(row, path, out, &issue)) return true;
    if (issue.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = issue;
    return false;
}
static bool skin_admit(frontend_remote_q2 *row, const char *path, const qa_material **out, qa_error *error)
{
    qa_scene_image_options options = {.family = QA_GAME_Q2, .usage = QA_IMAGE_USAGE_SKIN,
        .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
        .transparent = true, .transparent_index = 255};
    if (!qa_material_register(row->materials, path, &options, false, out, error)) return false;
    if (*out && (*out)->default_shader) *out = NULL;
    return true;
}
static const char *weapon_name(frontend_remote_q2 *row, uint32_t weapon)
{
    if (!weapon) return "weapon.md2";
    uint32_t count = 0;
    for (size_t i = 1; i < row->hud_names.layout.max_models && count < 31; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->hud_names.layout.models + i));
        if (name[0] == '#' && ++count == weapon) return name + 1;
    }
    return NULL;
}
static bool prepare_player(frontend_remote_q2 *row, uint32_t player, qa_error *error)
{
    const char *text=player==256?"male/grunt":frontend_remote_q2_config(row,(uint16_t)(row->hud_names.layout.player_skins+player));
    remote_q2_clientinfo value={0}; char model[64],skin[64]; appearance(row,text,model,skin);
    snprintf(value.model,sizeof(value.model),"players/%s/tris.md2",model);
    if (!model_admit(row,value.model,&value.model_resource,error)) return false;
    if (!value.model_resource && !equal(model,"male")) {
        strcpy(model,"male");strcpy(value.model,"players/male/tris.md2");
        if (!model_admit(row,value.model,&value.model_resource,error)) return false;
    }
    snprintf(value.skin,sizeof(value.skin),"players/%s/%s.pcx",model,skin);
    if (!skin_admit(row,value.skin,&value.skin_material,error)) return false;
    if (!value.skin_material && remote_q2_rerelease_presentation(row) && equal(model,"female")) {
        strcpy(skin,"athena");strcpy(value.skin,"players/female/athena.pcx");
        if (!skin_admit(row,value.skin,&value.skin_material,error)) return false;
    }
    if (!value.skin_material && !equal(model,"male")) {
        strcpy(model,"male");strcpy(value.model,"players/male/tris.md2");
        snprintf(value.skin,sizeof(value.skin),"players/male/%s.pcx",skin);
        if (!model_admit(row,value.model,&value.model_resource,error) ||
            !skin_admit(row,value.skin,&value.skin_material,error)) return false;
    }
    if (!value.skin_material) {
        snprintf(value.skin,sizeof(value.skin),"players/%s/grunt.pcx",model);
        if (!skin_admit(row,value.skin,&value.skin_material,error)) return false;
    }
    remote_q2_model *weapons[32]={0}; bool has_default_weapon=false;
    for (uint32_t i=0;i<32;++i) {
        const char *name=weapon_name(row,i); if (!name) break;
        char path[256];
        if (strlen(name)>127) return remote_q2_fail(error,QA_ERROR_FORMAT,"Q2 player weapon exceeds its native client-info path extent");
        snprintf(path,sizeof(path),"players/%s/%s",model,name);
        if (!model_admit(row,path,weapons+i,error)) return false;
        if (!i) has_default_weapon=weapons[i]!=NULL;
        if (!weapons[i] && equal(model,"cyborg")) {
            snprintf(path,sizeof(path),"players/male/%s",name);
            if (!model_admit(row,path,weapons+i,error)) return false;
        }
        if (!weapons[i] && i) weapons[i]=weapons[0];
        if (!weapons[i] && player!=256) {
            if (!model_admit(row,"players/male/weapon.md2",weapons+i,error)) return false;
        }
    }
    qa_string_id icon=player==256?row->hud_names.default_icon:row->hud_names.icons[player];
    qa_error issue={0};
    const qa_scene_image *image=qa_scene_q2_picture(row->images,row->hud_names.strings,icon,
        remote_q2_rerelease_presentation(row),QA_SCENE_LINEAR_MIPMAP_LINEAR,&issue);
    if (!image && issue.code!=QA_OK && issue.code!=QA_ERROR_NOT_FOUND) {if(error)*error=issue;return false;}
    value.valid=value.model_resource && value.skin_material && has_default_weapon && (remote_q2_rerelease_presentation(row) || image);
    if (!value.valid && player!=256) {
        value=row->clientinfo[256].value;
        memcpy(weapons,row->clientinfo[256].weapons,sizeof(weapons));
    }
    row->clientinfo[player].value=value;
    memcpy(row->clientinfo[player].weapons,weapons,sizeof(weapons));
    return true;
}
bool remote_q2_clientinfo_read(frontend_remote_q2 *row, uint32_t player, uint32_t weapon,
    remote_q2_clientinfo *out, qa_error *error)
{
    (void)error;
    if (!row || !out || player>256) return false;
    *out=row->clientinfo[player].value;
    const qa_cvar_view *vwep=qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_vwep);
    if ((vwep && !vwep->number) || weapon>=32) weapon=0;
    out->weapon_resource=row->clientinfo[player].weapons[weapon];
    if (!out->weapon_resource) out->weapon_resource=row->clientinfo[player].weapons[0];
    out->weapon=out->weapon_resource?out->weapon_resource->path:"";
    return true;
}
bool remote_q2_clientinfo_prepare(frontend_remote_q2 *row, qa_error *error)
{
    if (!prepare_player(row,256,error)) return false;
    for (uint32_t i=0;i<256;++i) {
        if (*frontend_remote_q2_config(row,(uint16_t)(row->hud_names.layout.player_skins+i))) {
            if (!prepare_player(row,i,error)) return false;
        } else row->clientinfo[i]=row->clientinfo[256];
    }
    const qa_cvar_view *setting=qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_noskins);
    row->clientinfo_noskins=setting?setting->number:0;
    return true;
}
bool remote_q2_clientinfo_config(frontend_remote_q2 *row, uint32_t index, qa_error *error)
{
    if (index>=row->hud_names.layout.player_skins && index-row->hud_names.layout.player_skins<256)
        return prepare_player(row,index-row->hud_names.layout.player_skins,error);
    if (index>=row->hud_names.layout.models && index-row->hud_names.layout.models<row->hud_names.layout.max_models)
        return remote_q2_clientinfo_prepare(row,error);
    return true;
}
bool remote_q2_clientinfo_settings(frontend_remote_q2 *row, qa_error *error)
{
    const qa_cvar_view *setting=qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_noskins);
    return row->clientinfo_noskins==(setting?setting->number:0) || remote_q2_clientinfo_prepare(row,error);
}
