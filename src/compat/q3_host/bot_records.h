#ifndef QA_Q3_HOST_BOT_RECORDS_H
#define QA_Q3_HOST_BOT_RECORDS_H

#include "internal.h"
#include "qa/bot_goals.h"

typedef enum q3_goal_fields { Q3_GOAL_FULL, Q3_GOAL_LEVEL_ITEM, Q3_GOAL_LOCATION } q3_goal_fields;
bool q3_bot_goal_read(q3_call *, uint64_t, qa_bot_goal *, qa_error *);
bool q3_bot_goal_copy(q3_call *, uint64_t, const qa_bot_goal *, qa_error *);
bool q3_bot_goal_fields(q3_call *, uint64_t, const qa_bot_goal *, q3_goal_fields, qa_error *);
bool q3_bot_weapon_copy(q3_call *, uint64_t, const qa_bot_weapon_info *,
                         const qa_bot_projectile_info *, qa_error *);
typedef struct q3_bot_memory { q3_call *call; uint64_t address; } q3_bot_memory;
bool q3_bot_inventory_read(void *, int32_t, int32_t *, qa_error *);
bool q3_bot_vector_read(void *, unsigned, float *, qa_error *);
bool q3_bot_vector_write(void *, unsigned, float, qa_error *);
bool q3_bot_vector_admit(void *, unsigned, qa_error *);

#endif
