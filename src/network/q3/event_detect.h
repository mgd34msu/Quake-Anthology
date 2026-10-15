#ifndef QA_Q3_EVENT_DETECT_H
#define QA_Q3_EVENT_DETECT_H
#include "qa/network_q3.h"

enum q3n_source_event {
    Q3N_EV_NONE, Q3N_EV_FOOTSTEP, Q3N_EV_FOOTSTEP_METAL, Q3N_EV_FOOTSPLASH,
    Q3N_EV_FOOTWADE, Q3N_EV_SWIM, Q3N_EV_STEP4, Q3N_EV_STEP8, Q3N_EV_STEP12, Q3N_EV_STEP16,
    Q3N_EV_FALL_SHORT, Q3N_EV_FALL_MEDIUM, Q3N_EV_FALL_FAR, Q3N_EV_JUMP_PAD,
    Q3N_EV_JUMP, Q3N_EV_WATER_TOUCH, Q3N_EV_WATER_LEAVE, Q3N_EV_WATER_UNDER, Q3N_EV_WATER_CLEAR,
    Q3N_EV_ITEM_PICKUP, Q3N_EV_GLOBAL_ITEM_PICKUP, Q3N_EV_NOAMMO, Q3N_EV_CHANGE_WEAPON,
    Q3N_EV_FIRE_WEAPON, Q3N_EV_USE_ITEM0, Q3N_EV_USE_ITEM1, Q3N_EV_USE_ITEM2, Q3N_EV_USE_ITEM3,
    Q3N_EV_USE_ITEM4, Q3N_EV_USE_ITEM5, Q3N_EV_USE_ITEM6, Q3N_EV_USE_ITEM7, Q3N_EV_USE_ITEM8,
    Q3N_EV_USE_ITEM9, Q3N_EV_USE_ITEM10, Q3N_EV_USE_ITEM11, Q3N_EV_USE_ITEM12,
    Q3N_EV_USE_ITEM13, Q3N_EV_USE_ITEM14, Q3N_EV_USE_ITEM15, Q3N_EV_ITEM_RESPAWN, Q3N_EV_ITEM_POP,
    Q3N_EV_TELEPORT_IN, Q3N_EV_TELEPORT_OUT, Q3N_EV_GRENADE_BOUNCE, Q3N_EV_GENERAL_SOUND,
    Q3N_EV_GLOBAL_SOUND, Q3N_EV_TEAM_SOUND, Q3N_EV_BULLET_FLESH, Q3N_EV_BULLET_WALL,
    Q3N_EV_MISSILE_HIT, Q3N_EV_MISSILE_MISS, Q3N_EV_MISSILE_METAL, Q3N_EV_RAIL,
    Q3N_EV_SHOTGUN, Q3N_EV_BULLET, Q3N_EV_PAIN, Q3N_EV_DEATH1, Q3N_EV_DEATH2, Q3N_EV_DEATH3,
    Q3N_EV_OBITUARY, Q3N_EV_QUAD, Q3N_EV_BATTLESUIT, Q3N_EV_REGEN, Q3N_EV_GIB, Q3N_EV_SCORE,
    Q3N_EV_PROX_STICK, Q3N_EV_PROX_TRIGGER, Q3N_EV_KAMIKAZE, Q3N_EV_OBELISK_EXPLODE,
    Q3N_EV_OBELISK_PAIN, Q3N_EV_INVUL_IMPACT, Q3N_EV_JUICED, Q3N_EV_LIGHTNING,
    Q3N_EV_DEBUG_LINE, Q3N_EV_STOP_LOOP, Q3N_EV_TAUNT, Q3N_EV_TAUNT_YES, Q3N_EV_TAUNT_NO,
    Q3N_EV_TAUNT_FOLLOW, Q3N_EV_TAUNT_FLAG, Q3N_EV_TAUNT_BASE, Q3N_EV_TAUNT_PATROL
};

static inline bool qa_q3_entity_event_detect(qa_q3_entity *event,
    int32_t *previous_event, bool *event_only_fired)
{
    if (event->eType > 13) {
        if (*previous_event) return false;
        if (event->eFlags & 16) event->number = event->otherEntityNum;
        *previous_event = 1;
        *event_only_fired = true;
        event->event = event->eType - 13;
    } else {
        if (event->event == *previous_event) return false;
        *previous_event = event->event;
        if (!(event->event & ~0x300)) return false;
    }
    return true;
}

static inline bool qa_q3_entity_audience_contains(uint32_t flags,
    int32_t single_client, uint32_t client)
{
    return !(flags & QA_Q3_SVF_NOCLIENT) &&
        (!(flags & QA_Q3_SVF_SINGLECLIENT) || single_client == (int32_t)client) &&
        (!(flags & QA_Q3_SVF_NOTSINGLECLIENT) || single_client != (int32_t)client) &&
        (!(flags & QA_Q3_SVF_CLIENTMASK) ||
            (client < 32 && ((uint32_t)single_client & (UINT32_C(1) << client))));
}
#endif
