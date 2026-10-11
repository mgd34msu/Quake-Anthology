#include "qa/player_state.h"
#include <string.h>

void qa_player_feedback_merge(qa_player_feedback *out,const qa_player_feedback *value)
{
    if(value->damage) {
        out->damage=true; out->armor+=value->armor; out->blood+=value->blood;
        memcpy(out->origin,value->origin,sizeof(out->origin));
    }
    if(value->set_angle) {
        out->set_angle=true; memcpy(out->angles,value->angles,sizeof(out->angles));
    }
}
