/* Port of src/compat/q2/rerelease/equipment-movement.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_EQUIPMENT_MOVEMENT_H
#define QA_COMPAT_Q2_RERELEASE_EQUIPMENT_MOVEMENT_H
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/movement.h"

void qa_compat_q2_prepare_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                       const qa_equipment_movement *equipment);
/* execute is the TS () => undefined. equipment NULL is TS undefined. */
void qa_compat_q2_with_rerelease_equipment_movement(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                                    const qa_equipment_movement *equipment, void *execute_ctx, void (*execute)(void *));
#endif
