#include "player/internal.h"
#include "qa/game_q2_bots.h"

bool qa_q2_bot_entity_read(qa_q2_game *game,qa_actor_id actor,qa_q2_bot_entity *out,qa_error *error) {
    if(!game || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot entity requires its source owner and output");return false;}
    qa_q2_visual visual;qa_builtin_actor_traits traits;
    *out=(qa_q2_bot_entity){0};
    if(!qa_q2_presentation_read(game,actor,&visual)) return true;
    if(!qa_q2_actor_traits(game,actor,&traits)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 source bot entity lost its native traits");return false;}
    const q2_actor *source=q2_actor_get(game,actor,false,NULL);
    if(source && source->projectile.kind!=Q2_PROJECTILE_NONE) traits.classname=source->projectile.classname;
    const char *classname=qa_strings_cstr(qa_session_strings(game->services.session),traits.classname);
    if(!classname) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 bot entity lost its source classname");return false;}
    *out=(qa_q2_bot_entity){.present=true,.model=visual.models[0],.classname=traits.classname,.frame=visual.frame,
        .max_health=traits.max_health,.hidden=!visual.visible,.worldspawn=!strcmp(classname,"worldspawn")};return true;
}
bool qa_q2_bot_clock_read(const qa_q2_game *game,uint64_t *time,bool *intermission,uint64_t *started,qa_error *error) {
    if(!game || !time || !intermission || !started) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot clock requires its actual source owner");return false;}
    *time=game->now_ns;*intermission=game->player_runtime && game->player_runtime->intermission;
    *started=*intermission?game->player_runtime->intermission_ns:0;return true;
}
bool qa_q2_bot_max_clients(const qa_q2_game *game,uint32_t *out,qa_error *error) {
    if(!game || !game->player_runtime || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot maxclients requires its configured source players");return false;}
    *out=game->player_runtime->rules.max_clients;return true;
}
bool qa_q2_bot_activate(qa_q2_game *game,qa_actor_id actor,qa_error *error) {
    if(!game) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 bot activation requires its source owner");return false;}
    q2_actor *source=q2_actor_get(game,actor,false,NULL);
    if(source && source->client) source->client->bot=true;
    return true;
}
qa_actor_id qa_q2_bot_world_actor(const qa_q2_game *game) {
    return game && game->services.physics?game->services.physics->world_actor:(qa_actor_id){0};
}
