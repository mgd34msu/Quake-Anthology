#ifndef QA_Q3_NATIVE_SELECTED_AUTHORED_MEDIA_INTERNAL_H
#define QA_Q3_NATIVE_SELECTED_AUTHORED_MEDIA_INTERNAL_H

#include "selected_authored_media.h"
#include "../q3/internal.h"

struct q3n_selected_authored_media {
    q3n_selected_authored_options options;
    char *gun, *anchor, *anchor_tag, *barrel, *flash;
    q3n_selected_authored_attachment *attachments;
    int32_t *attachment_models;
    int32_t gun_model, hands, barrel_model, flash_model;
    int32_t invisibility, battle_weapon, quad_weapon;
    bool gun_ready, view_ready, world_ready, hands_fallback, busy;
};
bool q3n_selected_authored_valid(const q3n_selected_authored_media *, qa_error *);

#endif
