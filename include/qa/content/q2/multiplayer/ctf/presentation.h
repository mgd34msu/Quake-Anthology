/* Port of src/content/q2/multiplayer/ctf/presentation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_PRESENTATION_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_PRESENTATION_H
#include "qa/content/q2/multiplayer/ctf/flags.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
const char *qa_ctf_tech(qa_q2_game_services *game, qa_actor_id actor); /* Q2CtfTech classname or NULL */
typedef struct qa_q2_ctf_presentation qa_q2_ctf_presentation;
qa_q2_ctf_presentation *qa_q2_ctf_presentation_create(qa_q2_ctf_context *context, qa_q2_ctf_flags *flags);
void qa_q2_ctf_presentation_destroy(qa_q2_ctf_presentation *presentation);
void qa_q2_ctf_presentation_scoreboard(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_ctf_presentation_identify(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id *out);
void qa_q2_ctf_presentation_hud(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game, const char *status);
/* Malloc'd location string. */
char *qa_q2_ctf_presentation_location(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_presentation_say_team(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game, const char *words);
void qa_q2_ctf_presentation_join_menu(qa_q2_ctf_presentation *p, qa_q2_entity *entity);
void qa_q2_ctf_presentation_stats(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *game);
#endif
