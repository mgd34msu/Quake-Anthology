#include "remote_q2_clientinfo.h"
#include "remote_q2_private.h"
#include "qa/material.h"
#include <stdio.h>
#include <string.h>

static bool skin_admit(frontend_remote_q2 *row, qa_string_id name, const qa_material **out, qa_error *error)
{
    qa_scene_image_options options = remote_q2_image_options(QA_IMAGE_USAGE_SKIN);
    if (!qa_material_register(row->materials, qa_strings_cstr(row->hud_names.strings, name), &options, false, out, error)) return false;
    if (*out && (*out)->default_shader) *out = NULL;
    return true;
}
static bool media_probe(void *context, qa_string_id name, qa_q2_player_media_kind kind,
    bool *found, qa_error *error)
{
    frontend_remote_q2 *row = context;
    qa_error issue = {0}; *found = false;
    if (kind == QA_Q2_PLAYER_MEDIA_MODEL) {
        remote_q2_model *model = NULL;
        if (!remote_q2_model_read_id(row, name, &model, &issue) && issue.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = issue;
            return false;
        }
        *found = model != NULL;
    } else if (kind == QA_Q2_PLAYER_MEDIA_SKIN) {
        const qa_material *skin = NULL;
        if (!skin_admit(row, name, &skin, error)) return false;
        *found = skin != NULL;
    } else {
        *found = qa_scene_q2_picture(row->images, row->hud_names.strings, name, QA_IMAGE_USAGE_PICTURE,
            remote_q2_rerelease_presentation(row), QA_SCENE_LINEAR_MIPMAP_LINEAR, &issue) != NULL;
        if (!*found && issue.code != QA_OK && issue.code != QA_ERROR_NOT_FOUND) { if (error) *error = issue; return false; }
    }
    return true;
}
static bool prepare_media(frontend_remote_q2 *row, uint32_t player, bool body, qa_error *error)
{
    remote_q2_clientinfo value = row->clientinfo[player].value;
    qa_hud_q2_player_ids base, *names = row->hud_names.players + (player == 256 ? 0 : player);
    if (player == 256) {
        if (!qa_hud_q2_player_ids_bind(row->hud_names.strings, "", remote_q2_rerelease_presentation(row), &base, error)) return false;
        names = &base;
    }
    const qa_cvar_view *setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_noskins);
    if (!qa_q2_player_media_resolve(row->hud_names.strings, names, row->hud_names.weapons.names,
        row->hud_names.weapons.count, remote_q2_rerelease_presentation(row), setting ? setting->number : 0,
        player == 256, body, media_probe, row, &value.media, error)) return false;
    remote_q2_model *weapons[32] = {0};
    if (body) {
        value.model = qa_strings_cstr(row->hud_names.strings, value.media.model);
        remote_q2_model_cached(row, value.media.model, &value.model_resource);
        if (!skin_admit(row, value.media.skin, &value.skin_material, error)) return false;
    }
    for (size_t i = 0; i < row->hud_names.weapons.count; ++i)
        remote_q2_model_cached(row, value.media.weapons[i], weapons + i);
    value.valid = value.media.body_valid && value.media.weapon_valid;
    if (!value.valid && player != 256) {
        value = row->clientinfo[256].value;
        memcpy(weapons, row->clientinfo[256].weapons, sizeof(weapons));
    }
    row->clientinfo[player].value = value;
    memcpy(row->clientinfo[player].weapons, weapons, sizeof(weapons));
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
    if (!prepare_media(row,256,true,error)) return false;
    for (uint32_t i=0;i<256;++i) {
        if (*frontend_remote_q2_config(row,(uint16_t)(row->hud_names.layout.player_skins+i))) {
            if (!prepare_media(row,i,true,error)) return false;
        } else row->clientinfo[i]=row->clientinfo[256];
    }
    const qa_cvar_view *setting=qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_noskins);
    row->clientinfo_noskins=setting?setting->number:0;
    return true;
}
bool remote_q2_clientinfo_config(frontend_remote_q2 *row, uint32_t index, bool weapon_changed, qa_error *error)
{
    if (index>=row->hud_names.layout.player_skins && index-row->hud_names.layout.player_skins<256)
        return prepare_media(row,index-row->hud_names.layout.player_skins,true,error);
    if (weapon_changed) {
        for (uint32_t player=256;;--player) {
            if (!prepare_media(row,player,false,error)) return false;
            if (!player) break;
        }
    }
    return true;
}
bool remote_q2_clientinfo_settings(frontend_remote_q2 *row, qa_error *error)
{
    const qa_cvar_view *setting=qa_cvars_read(row->options.domain.cvars,row->cvar_handles.cl_noskins);
    return row->clientinfo_noskins==(setting?setting->number:0) || remote_q2_clientinfo_prepare(row,error);
}
