#ifndef QA_Q2_MONSTER_MEDIC_H
#define QA_Q2_MONSTER_MEDIC_H

#include "internal.h"

bool q2m_medic_callback(q2m_context *, q2m_callback_id, bool *handled, qa_error *);
bool q2m_medic_check_attack(q2m_context *, bool *handled, bool *selected, bool *started,
                           qa_error *);
bool q2m_medic_attack_move(q2m_context *, float distance, q2m_move_id *, qa_error *);
bool q2m_medic_cleanup_patient(q2m_context *, qa_actor_id, qa_error *);
bool q2m_medic_abort(q2m_context *, bool change_frame, bool gib, bool mark, qa_error *);
bool q2m_medic_died(q2m_context *, qa_error *);
bool q2m_fixbot_repair(q2m_context *, qa_error *);
void q2m_fixbot_flight(q2m_context *, bool heal, bool weld);
bool q2m_fixbot_attack(q2m_context *, qa_error *);
bool q2m_revive(q2m_context *patient, qa_error *);
bool q2m_hunt_target(q2m_context *, bool animate_state, qa_error *);

#endif
