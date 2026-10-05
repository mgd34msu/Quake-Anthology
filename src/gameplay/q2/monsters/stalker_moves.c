#include "internal.h"

enum stalker_action {
    ST_NONE, ST_IDLE, ST_HEAL, ST_SHOOT, ST_SHOOT_AGAIN, ST_SWING,
    ST_JUMP, ST_WAIT, ST_UP, ST_DOWN, ST_STEP
};
static const q2m_frame_action actions[] = {
    {NULL, INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_idle_noise], INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_heal], INT_MIN},
    {&q2m_callbacks[Q2M_CALLBACK_stalker_shoot_attack], INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_shoot_attack2], INT_MIN},
    {&q2m_callbacks[Q2M_CALLBACK_stalker_swing_attack], INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_jump_straightup], INT_MIN},
    {&q2m_callbacks[Q2M_CALLBACK_stalker_jump_wait_land], INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_jump_up], INT_MIN},
    {&q2m_callbacks[Q2M_CALLBACK_stalker_jump_down], INT_MIN}, {&q2m_callbacks[Q2M_CALLBACK_stalker_footstep], INT_MIN}
};
#define F(AI, DISTANCE, ACTION) { .ai = Q2M_AI_##AI, .distance = DISTANCE, \
    .lerp_frame = -1, .action_first = ACTION, .action_count = (ACTION != ST_NONE) }
#define S F(STAND, 0, ST_NONE)
#define M F(MOVE, 0, ST_NONE)
enum stalker_frames {
    ST_IDLE_FIRST = 0, ST_IDLE2_FIRST = ST_IDLE_FIRST + 21,
    ST_RUN_FIRST = ST_IDLE2_FIRST + 13, ST_WALK_FIRST = ST_RUN_FIRST + 4,
    ST_REACTIVATE_FIRST = ST_WALK_FIRST + 8, ST_HEAL_FIRST = ST_REACTIVATE_FIRST + 4,
    ST_FALSE_FIRST = ST_HEAL_FIRST + 10, ST_PAIN_FIRST = ST_FALSE_FIRST + 9,
    ST_SHOOT_FIRST = ST_PAIN_FIRST + 4, ST_LEFT_FIRST = ST_SHOOT_FIRST + 4,
    ST_RIGHT_FIRST = ST_LEFT_FIRST + 8, ST_JUMP_FIRST = ST_RIGHT_FIRST + 5,
    ST_UP_FIRST = ST_JUMP_FIRST + 4, ST_DOWN_FIRST = ST_UP_FIRST + 7,
    ST_DEATH_FIRST = ST_DOWN_FIRST + 7, ST_FRAME_COUNT = ST_DEATH_FIRST + 9
};
static const q2m_frame frames[] = {
    S, S, S, S, S, S, F(STAND, 0, ST_IDLE),
    S, S, S, S, S, S, S, S, S, S, S, S, S, S,
    S, S, S, S, S, S, S, S, S, S, S, S, S,
    F(RUN, 13, ST_STEP), F(RUN, 17, ST_NONE), F(RUN, 21, ST_STEP), F(RUN, 18, ST_NONE),
    F(WALK, 4, ST_STEP), F(WALK, 6, ST_NONE), F(WALK, 8, ST_NONE), F(WALK, 5, ST_NONE),
    F(WALK, 4, ST_STEP), F(WALK, 6, ST_NONE), F(WALK, 8, ST_NONE), F(WALK, 4, ST_NONE),
    M, M, M, F(MOVE, 0, ST_STEP),
    F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL),
    F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL),
    F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL), F(MOVE, 0, ST_HEAL),
    M, M, M, M, M, M, M, M, M,
    M, M, M, M,
    F(CHARGE, 13, ST_NONE), F(CHARGE, 17, ST_SHOOT),
    F(CHARGE, 21, ST_NONE), F(CHARGE, 18, ST_SHOOT_AGAIN),
    F(CHARGE, 2, ST_NONE), F(CHARGE, 4, ST_NONE), F(CHARGE, 6, ST_NONE),
    F(CHARGE, 10, ST_STEP), F(CHARGE, 5, ST_SWING), F(CHARGE, 5, ST_NONE),
    F(CHARGE, 5, ST_NONE), F(CHARGE, 5, ST_STEP),
    F(CHARGE, 4, ST_NONE), F(CHARGE, 6, ST_STEP), F(CHARGE, 6, ST_SWING),
    F(CHARGE, 10, ST_NONE), F(CHARGE, 5, ST_STEP),
    F(MOVE, 1, ST_JUMP), F(MOVE, 1, ST_WAIT), F(MOVE, -1, ST_STEP), F(MOVE, -1, ST_NONE),
    F(MOVE, -8, ST_NONE), F(MOVE, -8, ST_NONE), F(MOVE, -8, ST_NONE), F(MOVE, -8, ST_NONE),
    F(MOVE, 0, ST_UP), F(MOVE, 0, ST_WAIT), F(MOVE, 0, ST_STEP),
    M, M, M, M, F(MOVE, 0, ST_DOWN), F(MOVE, 0, ST_WAIT), F(MOVE, 0, ST_STEP),
    M, F(MOVE, -5, ST_NONE), F(MOVE, -10, ST_NONE), F(MOVE, -20, ST_NONE),
    F(MOVE, -10, ST_NONE), F(MOVE, -10, ST_NONE), F(MOVE, -5, ST_NONE),
    F(MOVE, -5, ST_NONE), F(MOVE, 0, ST_STEP)
};
_Static_assert(sizeof(frames) / sizeof(*frames) == ST_FRAME_COUNT,
               "Rerelease Stalker authored frame offsets");
#define MOVE(NAME, FIRST, LAST, END, OFFSET) \
    {Q2M_MOVE_stalker_move_##NAME, \
     Q2M_MOVE_stalker_move_##NAME == Q2M_MOVE_stalker_move_walk ? Q2M_MOVE_WALKING : 0, \
     FIRST, LAST, END, 0, OFFSET}
static const q2m_move moves[] = {
    MOVE(idle, 0, 20, &q2m_callbacks[Q2M_CALLBACK_stalker_stand], ST_IDLE_FIRST),
    MOVE(idle2, 21, 33, &q2m_callbacks[Q2M_CALLBACK_stalker_stand], ST_IDLE2_FIRST),
    MOVE(stand, 0, 20, &q2m_callbacks[Q2M_CALLBACK_stalker_stand], ST_IDLE_FIRST),
    MOVE(run, 49, 52, NULL, ST_RUN_FIRST),
    MOVE(walk, 34, 41, &q2m_callbacks[Q2M_CALLBACK_stalker_walk], ST_WALK_FIRST),
    MOVE(false_death_end, 89, 92, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_REACTIVATE_FIRST),
    MOVE(false_death, 79, 88, &q2m_callbacks[Q2M_CALLBACK_stalker_false_death], ST_HEAL_FIRST),
    MOVE(false_death_start, 70, 78, &q2m_callbacks[Q2M_CALLBACK_stalker_false_death], ST_FALSE_FIRST),
    MOVE(pain, 66, 69, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_PAIN_FIRST),
    MOVE(shoot, 49, 52, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_SHOOT_FIRST),
    MOVE(swing_l, 53, 60, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_LEFT_FIRST),
    MOVE(swing_r, 61, 65, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_RIGHT_FIRST),
    MOVE(jump_straightup, 45, 48, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_JUMP_FIRST),
    MOVE(jump_up, 42, 48, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_UP_FIRST),
    MOVE(jump_down, 42, 48, &q2m_callbacks[Q2M_CALLBACK_stalker_run], ST_DOWN_FIRST),
    MOVE(death, 70, 78, &q2m_callbacks[Q2M_CALLBACK_stalker_dead], ST_DEATH_FIRST)
};

static const uint16_t move_index[Q2M_MOVE_COUNT] = {
    [Q2M_MOVE_stalker_move_idle]=1u,
    [Q2M_MOVE_stalker_move_idle2]=2u,
    [Q2M_MOVE_stalker_move_stand]=3u,
    [Q2M_MOVE_stalker_move_run]=4u,
    [Q2M_MOVE_stalker_move_walk]=5u,
    [Q2M_MOVE_stalker_move_false_death_end]=6u,
    [Q2M_MOVE_stalker_move_false_death]=7u,
    [Q2M_MOVE_stalker_move_false_death_start]=8u,
    [Q2M_MOVE_stalker_move_pain]=9u,
    [Q2M_MOVE_stalker_move_shoot]=10u,
    [Q2M_MOVE_stalker_move_swing_l]=11u,
    [Q2M_MOVE_stalker_move_swing_r]=12u,
    [Q2M_MOVE_stalker_move_jump_straightup]=13u,
    [Q2M_MOVE_stalker_move_jump_up]=14u,
    [Q2M_MOVE_stalker_move_jump_down]=15u,
    [Q2M_MOVE_stalker_move_death]=16u,
};
const q2m_move_set *q2m_stalker_rerelease_moves(void) {
    static const q2m_move_set value = {
        .key = "rerelease/stalker:stalkerMoves",
        .moves = moves, .move_index = move_index, .move_count = sizeof(moves) / sizeof(*moves),
        .frames = frames, .frame_count = sizeof(frames) / sizeof(*frames),
        .actions = actions, .action_count = sizeof(actions) / sizeof(*actions)
    };
    return &value;
}
#undef F
#undef S
#undef M
#undef MOVE
