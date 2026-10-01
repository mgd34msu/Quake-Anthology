#ifndef QA_Q3_NATIVE_SELECTED_MEDIA_INTERNAL_H
#define QA_Q3_NATIVE_SELECTED_MEDIA_INTERNAL_H
#include "selected_media.h"
#include "../q3/internal.h"
#include "qa/vfs_view_save.h"

typedef struct q3n_selected_media_row {
    int32_t gun, barrel, flash, hands;
    bool world_ready, view_ready, hands_fallback;
} q3n_selected_media_row;
struct q3n_selected_media {
    q3n_selected_media_options options;
    q3n_selected_media_row rows[14];
    int32_t invisibility, battle_weapon, quad_weapon;
    qa_vfs *animation_content;
    qa_resource *animation_resource;
    qa_vfs_acquisition animation_receipt;
    qa_player_animation_config animation_config;
    bool shaders_ready, character, busy;
};
const qa_q3_item *q3n_selected_media_item(qa_q3_product, int32_t);
bool q3n_selected_media_path(const char *, const char *, char [128], qa_error *);
bool q3n_selected_media_valid(const q3n_selected_media *, bool capture, qa_error *);
#endif
