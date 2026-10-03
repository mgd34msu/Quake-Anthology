#include "maps/internal.h"
#include "qa/game_q1_bots.h"

bool qa_q1_bot_entity_read(const qa_q1_game *game,qa_actor_id actor,qa_q1_bot_entity *out,qa_error *error) {
    if(!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot entity requires its source owner and output");return false;}
    const q1_actor *source=q1_entity_const(game,actor);
    *out=(qa_q1_bot_entity){0};if(!source) return true;
    qa_q1_player_view player;
    const char *classname=source->classname
        ? qa_strings_cstr(qa_session_strings(game->services.session),source->classname) : NULL;
    if(source->classname && !classname) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q1 bot entity lost its source classname");return false;}
    *out=(qa_q1_bot_entity){.present=true,.model=source->model,.classname=source->classname,.frame=source->frame,
        .max_health=qa_q1_player_read(game,actor,&player)?player.max_health:source->max_health,
        .worldspawn=classname && !strcmp(classname,"worldspawn")};return true;
}
bool qa_q1_bot_clock_read(const qa_q1_game *game,double *time,bool *intermission,double *exit_after,qa_error *error) {
    if(!game || !time || !intermission || !exit_after) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot clock requires its actual source owner");return false;}
    const qa_q1_level_state *level=game->maps?qa_q1_level_read(game->maps->options.level):NULL;
    *time=game->time;*intermission=level && level->intermission;
    *exit_after=*intermission?level->exit_after:0;return true;
}
qa_actor_id qa_q1_bot_world_actor(const qa_q1_game *game) {
    return game && game->maps?game->maps->world_actor:(qa_actor_id){0};
}
bool qa_q1_bot_max_clients(const qa_q1_game *game,uint32_t *out,qa_error *error) {
    if(!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot maxclients requires its source owner");return false;}
    *out=game->options.max_clients;return true;
}
