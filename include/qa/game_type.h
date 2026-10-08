#ifndef QA_GAME_TYPE_H
#define QA_GAME_TYPE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum qa_game_type {
    QA_GAME_TYPE_FFA,
    QA_GAME_TYPE_DUEL,
    QA_GAME_TYPE_ARENA_SINGLE_PLAYER,
    QA_GAME_TYPE_TEAM_DEATHMATCH,
    QA_GAME_TYPE_CTF,
    QA_GAME_TYPE_ONE_FLAG,
    QA_GAME_TYPE_OVERLOAD,
    QA_GAME_TYPE_HARVESTER,
    QA_GAME_TYPE_CAMPAIGN,
    QA_GAME_TYPE_COOPERATIVE
} qa_game_type;

static inline bool qa_game_type_is_team(int32_t type)
{
    return type >= QA_GAME_TYPE_TEAM_DEATHMATCH && type <= QA_GAME_TYPE_HARVESTER;
}

static inline bool qa_game_type_is_objective(int32_t type)
{
    return type >= QA_GAME_TYPE_CTF && type <= QA_GAME_TYPE_HARVESTER;
}

static inline bool qa_game_type_has_allies(int32_t type)
{
    return qa_game_type_is_team(type) || type == QA_GAME_TYPE_COOPERATIVE;
}

#endif
