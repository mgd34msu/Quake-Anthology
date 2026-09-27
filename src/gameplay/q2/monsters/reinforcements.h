#ifndef QA_Q2_MONSTER_REINFORCEMENTS_H
#define QA_Q2_MONSTER_REINFORCEMENTS_H
#include "internal.h"

typedef struct q2m_reinforcement {
    const q2m_definition *definition;
    int strength;
    qa_bounds bounds;
} q2m_reinforcement;
typedef struct q2m_summon_state {
    qa_string_id authored;
    q2m_reinforcement *entries;
    size_t entry_count;
    q2m_reinforcement chosen[5];
    size_t chosen_count;
    int classic_strength;
    bool configured;
} q2m_summon_state;

bool q2m_create_reinforcement(q2m_context *, const char *, qa_vec3, qa_actor_id *, qa_error *);
bool q2m_widow_slots(q2m_context *, qa_error *);
bool q2m_widow_summon(q2m_context *, bool second, bool grow, qa_error *);
bool q2m_widow_attack_move(q2m_context *, bool second, float distance, const char **, qa_error *);
bool q2m_medic_summon_initialize(q2m_context *, q2m_summon_state *, qa_error *);
bool q2m_medic_determine_summons(q2m_context *, q2m_summon_state *, qa_error *);
bool q2m_medic_grow_summons(q2m_context *, q2m_summon_state *, qa_error *);
bool q2m_medic_finish_summons(q2m_context *, q2m_summon_state *, qa_error *);
void q2m_summon_clear(q2m_summon_state *);
bool q2m_summon_add(int64_t *counter, int64_t amount, qa_error *);
bool q2m_summon_subtract(int64_t *counter, int64_t amount, qa_error *);
bool q2m_summon_has_slots(const struct qa_q2_monster *, uint64_t required);
bool q2m_summon_initialize(q2m_context *, qa_error *);
bool q2m_summon_callback(q2m_context *, const char *, bool *handled, qa_error *);
bool q2m_rerelease_spawn_growth(qa_q2_game *, qa_vec3, float radius, qa_error *);
bool q2m_rerelease_growth_tick(qa_q2_game *, q2_actor *, qa_error *);
/* Direct source attack entry, without another AI eligibility/yaw turn. */
bool q2m_source_attack(q2m_context *, bool melee, qa_error *);

#endif
