#include "qa/network_q3.h"
#include "qa/binary.h"
#include "qa/text.h"
#include <string.h>

void qa_q3_player_entity_state(qa_q3_player *player,qa_q3_entity *entity,
    bool snap,bool extrapolate,int32_t time_ms)
{
    entity->eType=player->pmType==2 || player->pmType==5 || player->stats[0]<=-40?10:1;
    entity->number=player->clientNum;
    entity->pos.type=extrapolate?3:1;
    memcpy(entity->pos.base,player->origin,sizeof(entity->pos.base));
    memcpy(entity->pos.delta,player->velocity,sizeof(entity->pos.delta));
    if(extrapolate){entity->pos.time=time_ms;entity->pos.duration=50;}
    entity->apos.type=1;
    memcpy(entity->apos.base,player->viewangles,sizeof(entity->apos.base));
    if(snap)for(size_t i=0;i<3;++i){
        entity->pos.base[i]=(float)qa_source_to_i32(entity->pos.base[i]);
        entity->apos.base[i]=(float)qa_source_to_i32(entity->apos.base[i]);
    }
    entity->angles2[1]=(float)player->movementDir;
    entity->legsAnim=player->legsAnim;entity->torsoAnim=player->torsoAnim;
    entity->clientNum=player->clientNum;
    entity->eFlags=player->stats[0]<=0?player->eFlags|1:player->eFlags&~1;
    if(player->externalEvent){
        entity->event=player->externalEvent;entity->eventParm=player->externalEventParm;
    }else if(player->entityEventSequence<player->eventSequence){
        int32_t oldest=qa_i32_sub(player->eventSequence,2);
        if(player->entityEventSequence<oldest)player->entityEventSequence=oldest;
        uint32_t sequence=(uint32_t)player->entityEventSequence;
        entity->event=player->events[sequence&1u]|(int32_t)((sequence&3u)<<8);
        entity->eventParm=player->eventParms[sequence&1u];
        player->entityEventSequence=qa_i32_from_u32(sequence+1u);
    }
    entity->weapon=player->weapon;entity->groundEntityNum=player->groundEntityNum;
    entity->powerups=0;
    for(unsigned i=0;i<16;++i)if(player->powerups[i])entity->powerups|=(int32_t)(1u<<i);
    entity->loopSound=player->loopSound;entity->generic1=player->generic1;
}
