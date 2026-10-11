#ifndef QA_PLAYER_NAMES_H
#define QA_PLAYER_NAMES_H
#include "qa/strings.h"

typedef struct qa_hud_q2_player_names { qa_bytes name, appearance, tag, model, skin; } qa_hud_q2_player_names;
qa_hud_q2_player_names qa_hud_q2_player_names_read(const char *, bool rerelease);
typedef struct qa_hud_q2_player_ids {
    qa_string_id name, appearance, model, skin, icon, tag, native_tag;
} qa_hud_q2_player_ids;
bool qa_hud_q2_player_ids_bind(qa_strings *, const char *, bool rerelease, qa_hud_q2_player_ids *, qa_error *);
bool qa_hud_q2_player_model_path(qa_bytes model, qa_bytes member, const char *suffix, char *, size_t, qa_error *);
bool qa_hud_q2_player_icon_path(qa_bytes appearance, char *, size_t, qa_error *);
bool qa_hud_q2_player_picture_names(qa_strings *, const char *, qa_string_id *, qa_string_id *, qa_error *);
bool qa_hud_q2_player_icon(qa_strings *, const char *, size_t, qa_string_id *, qa_error *);
qa_hud_q2_player_names qa_hud_q2_appearance_names_read(const char *, bool rerelease);
bool qa_player_skin_bind(qa_strings *, const char *, bool rerelease, qa_string_id *skin, qa_string_id *model, qa_error *);
typedef enum qa_q2_player_media_kind {
    QA_Q2_PLAYER_MEDIA_MODEL, QA_Q2_PLAYER_MEDIA_SKIN, QA_Q2_PLAYER_MEDIA_ICON
} qa_q2_player_media_kind;
typedef struct qa_q2_player_media {
    qa_string_id model_name, model, skin, icon, weapons[32];
    bool body_valid, weapon_valid;
} qa_q2_player_media;
typedef bool (*qa_q2_player_media_probe)(void *, qa_string_id, qa_q2_player_media_kind, bool *, qa_error *);
/* Resolve original player-media fallbacks once at configuration admission.
 * A weapon-only update keeps the retained body and model-name IDs. */
bool qa_q2_player_media_resolve(qa_strings *, const qa_hud_q2_player_ids *, const qa_string_id *, size_t,
    bool rerelease, double noskins, bool base_client, bool body, qa_q2_player_media_probe, void *,
    qa_q2_player_media *, qa_error *);
#endif
