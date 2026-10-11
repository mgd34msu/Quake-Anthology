#include "qa/player_names.h"
#include <stdio.h>
#include <string.h>

qa_hud_q2_player_names qa_hud_q2_appearance_names_read(const char *appearance, bool rerelease)
{
    const char *slash = strchr(appearance, '/');
    const char *end = strchr(appearance, '\\');
    if (slash && end && slash > end) slash = NULL;
    if (!slash && !rerelease) slash = end;
    const char *skin = slash ? slash + 1 : "grunt";
    const char *skin_end = strchr(skin, '\\');
    const char *appearance_end = slash ? skin_end : end;
    const char *tag = appearance_end && appearance_end[1] ? appearance_end + 1 : "default";
    const char *tag_end = strchr(tag, '\\');
    return (qa_hud_q2_player_names){
        .appearance = {(const uint8_t *)appearance, appearance_end ? (size_t)(appearance_end - appearance) : strlen(appearance)},
        .tag = {(const uint8_t *)tag, tag_end ? (size_t)(tag_end - tag) : strlen(tag)},
        .model = slash && slash != appearance ? (qa_bytes){(const uint8_t *)appearance, (size_t)(slash - appearance)} :
            (qa_bytes){(const uint8_t *)"male", 4},
        .skin = {(const uint8_t *)skin, skin_end ? (size_t)(skin_end - skin) : strlen(skin)}};
}
qa_hud_q2_player_names qa_hud_q2_player_names_read(const char *info, bool rerelease)
{
    const char *first = strchr(info, '\\');
    qa_hud_q2_player_names value = qa_hud_q2_appearance_names_read(first && first[1] ? first + 1 : "male/grunt", rerelease);
    value.name = (qa_bytes){(const uint8_t *)info, first ? (size_t)(first - info) : 0};
    return value;
}
bool qa_player_skin_bind(qa_strings *strings, const char *text, bool rerelease,
    qa_string_id *skin, qa_string_id *model, qa_error *error)
{
    qa_hud_q2_player_names names = qa_hud_q2_appearance_names_read(text, rerelease);
    qa_string_id skin_id, model_id;
    if (!qa_strings_intern_cstr(strings, text, &skin_id, error) ||
        !qa_strings_intern(strings, names.model, &model_id, error)) return false;
    *skin = skin_id; *model = model_id;
    return true;
}

bool qa_hud_q2_player_model_path(qa_bytes model, qa_bytes member, const char *suffix,
    char *path, size_t capacity, qa_error *error)
{
    int n = snprintf(path, capacity, "players/%.*s/%.*s%s", (int)model.size, (const char *)model.data,
        (int)member.size, (const char *)member.data, suffix);
    if (n < 0 || (size_t)n >= capacity) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 player model path is too long"); return false; }
    return true;
}
bool qa_hud_q2_player_icon_path(qa_bytes appearance,char *path,size_t capacity,qa_error *error)
{
    int n=snprintf(path,capacity,"/players/%.*s_i.pcx",(int)appearance.size,(const char *)appearance.data);
    if(n<0||(size_t)n>=capacity) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 player icon path is too long");return false;}
    return true;
}
bool qa_hud_q2_player_icon(qa_strings *strings,const char *appearance,size_t length,qa_string_id *icon,qa_error *error)
{
    char path[4096];
    if (!qa_hud_q2_player_icon_path((qa_bytes){(const uint8_t *)appearance,length},path,sizeof(path),error)) return false;
    return qa_strings_intern_cstr(strings,path,icon,error);
}
bool qa_hud_q2_player_ids_bind(qa_strings *strings, const char *info, bool rerelease,
    qa_hud_q2_player_ids *out, qa_error *error)
{
    qa_hud_q2_player_names names = qa_hud_q2_player_names_read(info, rerelease);
    qa_hud_q2_player_ids value = {0};
    char path[4096];
    if (!qa_strings_intern(strings, names.name, &value.name, error) ||
        !qa_strings_intern(strings, names.appearance, &value.appearance, error) ||
        !qa_strings_intern(strings, names.model, &value.model, error) ||
        !qa_strings_intern(strings, names.skin, &value.skin, error) ||
        !qa_hud_q2_player_icon(strings, (const char *)names.appearance.data, names.appearance.size, &value.icon, error)) return false;
    int n = snprintf(path, sizeof(path), "/tags/%.*s.pcx", (int)names.tag.size, (const char *)names.tag.data);
    if (n < 0 || (size_t)n >= sizeof(path) || !qa_strings_intern_cstr(strings, path, &value.tag, error)) return false;
    n = snprintf(path, sizeof(path), "%.*s.pcx", (int)names.tag.size, (const char *)names.tag.data);
    if (n < 0 || (size_t)n >= sizeof(path) || !qa_strings_intern_cstr(strings, path, &value.native_tag, error)) return false;
    *out = value;
    return true;
}
bool qa_hud_q2_player_picture_names(qa_strings *strings, const char *info, qa_string_id *icon, qa_string_id *dogtag, qa_error *error)
{
    qa_hud_q2_player_ids names;
    if (!qa_hud_q2_player_ids_bind(strings, info, true, &names, error)) return false;
    *icon = names.icon; *dogtag = names.tag;
    return true;
}
