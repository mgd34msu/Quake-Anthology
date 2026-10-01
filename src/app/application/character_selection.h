#ifndef QA_APPLICATION_CHARACTER_SELECTION_PRIVATE_H
#define QA_APPLICATION_CHARACTER_SELECTION_PRIVATE_H
#include "internal.h"
#include "qa/application_character_selection.h"
bool application_character_userinfo(qa_catalog *, const qa_launch_choices *,
    const qa_launch_seat *, qa_game_family protocol, bool local_ip,
    char *, size_t, qa_error *);
bool application_character_q2_initial_skin(qa_catalog *, const qa_launch_choices *,
    const qa_launch_seat *, const char *source_info, char *, size_t, qa_error *);
#endif
