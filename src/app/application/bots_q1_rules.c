#include "internal.h"
#include "bots_q1_rules.h"
#include <math.h>

static bool live(application_provider *source,qa_actor_id actor)
{
    qa_q1_player_view player;
    return source && source->kind==APPLICATION_PROVIDER_Q1 && source->constructed &&
        source->attached && !source->close_pending &&
        qa_q1_player_read(source->state.q1,actor,&player);
}

bool application_bot_q1_nail_speed(void *opaque,qa_actor_id actor,float base,
                                   float *speed,qa_error *error)
{
    application_provider *source=opaque;
    if(!speed || !isfinite(base) || base<0 || !live(source,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q1 bot nail speed requires its retained source player");
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(source->state.q1,&operation,error)) return false;
    qa_application *app=source->application;
    *speed=base;
    bool okay=true;
    for(size_t i=0;okay && app->modes && i<app->mode_count;++i) {
        qa_mode_view view;
        qa_mode_id id=app->mode_ids[i];
        okay=qa_modes_read(app->modes,id,&view,error);
        if(!okay || !view.rules.enabled || view.rules.source!=QA_MODE_THREEWAVE ||
           !qa_modes_has_relic(app->modes,id,actor,QA_RELIC_HASTE)) continue;
        bool handled;
        okay=qa_modes_tech_sound(app->modes,id,actor,QA_RELIC_HASTE,false,false,&handled,error);
        if(okay) *speed=2000;
        if(okay && (!qa_q1_game_operation_live(&operation) || !live(source,actor)))
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Q1 bot nail-speed cue retired its source player");
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
