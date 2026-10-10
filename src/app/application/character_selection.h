#ifndef QA_APPLICATION_CHARACTER_SELECTION_PRIVATE_H
#define QA_APPLICATION_CHARACTER_SELECTION_PRIVATE_H
#include "internal.h"
#include "qa/application_character_selection.h"
typedef struct application_character_names {
    qa_strings *strings;
    qa_string_id definition, model, skin, head_model, head_skin;
} application_character_names;
bool application_character_names_retain(qa_strings *,
    const qa_application_character_declaration *, application_character_names *,
    qa_native_q3_character_selection *, qa_error *);
bool application_character_names_match(const application_character_names *,
    const qa_application_character_declaration *);
void application_character_names_release(application_character_names *);
bool application_character_userinfo(qa_application *,qa_catalog *,const qa_launch_choices *,
    const qa_launch_seat *, qa_game_family protocol, bool local_ip,
    char *, size_t, qa_error *);
bool application_character_q2_initial_userinfo(const char *source_info,const char *defaults,
    char *,size_t,qa_error *);
#endif
