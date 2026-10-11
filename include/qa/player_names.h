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
#endif
