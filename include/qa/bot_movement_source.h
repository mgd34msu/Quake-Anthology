#ifndef QA_BOT_MOVEMENT_SOURCE_H
#define QA_BOT_MOVEMENT_SOURCE_H
#include "qa/bot_movement.h"
#include "qa/bot_source.h"

/* Borrowed for one synchronous operation. Native callers supply value; module
 * callers supply checked semantic fields, with byte layouts kept in the host. */
typedef struct qa_bot_move_goal_source {
    const qa_bot_goal *value;
    void *context;
    bool (*area)(void *, int32_t *, qa_error *);
    qa_bot_vector_source origin;
} qa_bot_move_goal_source;
typedef enum qa_bot_move_result_field {
    QA_BOT_RESULT_FAILURE,
    QA_BOT_RESULT_TYPE,
    QA_BOT_RESULT_BLOCKED,
    QA_BOT_RESULT_BLOCK_ENTITY,
    QA_BOT_RESULT_TRAVEL_TYPE,
    QA_BOT_RESULT_FLAGS,
    QA_BOT_RESULT_WEAPON
} qa_bot_move_result_field;
typedef enum qa_bot_move_result_vector {
    QA_BOT_RESULT_DIRECTION,
    QA_BOT_RESULT_VIEW
} qa_bot_move_result_vector;
typedef struct qa_bot_move_result_io {
    qa_bot_move_result *value;
    void *context;
    bool (*read)(void *, qa_bot_move_result_field, int32_t *, qa_error *);
    bool (*write)(void *, qa_bot_move_result_field, int32_t, qa_error *);
    bool (*write_vector)(void *, qa_bot_move_result_vector, unsigned component, float,
                         qa_error *);
} qa_bot_move_result_io;
typedef enum qa_bot_move_init_field {
    QA_BOT_INIT_ENTITY,
    QA_BOT_INIT_CLIENT,
    QA_BOT_INIT_PRESENCE,
    QA_BOT_INIT_FLAGS
} qa_bot_move_init_field;
typedef enum qa_bot_move_init_vector {
    QA_BOT_INIT_ORIGIN,
    QA_BOT_INIT_VELOCITY,
    QA_BOT_INIT_VIEW_OFFSET,
    QA_BOT_INIT_VIEW_ANGLES
} qa_bot_move_init_vector;
typedef struct qa_bot_move_init_source {
    const qa_bot_move_input *value;
    void *context;
    bool (*integer)(void *, qa_bot_move_init_field, int32_t *, qa_error *);
    bool (*vector)(void *, qa_bot_move_init_vector, unsigned component, float *, qa_error *);
    bool (*think_time)(void *, float *, qa_error *);
} qa_bot_move_init_source;

bool qa_bot_moves_initialize_from(qa_bot_moves *, uint32_t,
                                  const qa_bot_move_init_source *, qa_error *);
bool qa_bot_moves_direction_from(qa_bot_moves *, uint32_t, const qa_bot_vector_source *,
                                 float speed, uint32_t type, bool *, qa_error *);
bool qa_bot_moves_avoid_spot_from(qa_bot_moves *, uint32_t, const qa_bot_vector_source *,
                                  float radius, int32_t type, qa_error *);
bool qa_bot_moves_goal_from(qa_bot_moves *, uint32_t, const qa_bot_move_goal_source *,
                            uint32_t travel_flags, const qa_bot_move_result_io *, qa_error *);
/* The source module view query uses base navigation. The native value query
 * instead follows its retained movement state's selected client. */
bool qa_bot_moves_view_target_from(qa_bot_moves *, uint32_t, const qa_bot_move_goal_source *,
                                   uint32_t travel_flags, float look_ahead,
                                   const qa_bot_vector_target *, bool *, qa_error *);
bool qa_bot_moves_visible_position_from(qa_bot_moves *,
                                        const qa_bot_vector_source *, uint32_t area,
                                        const qa_bot_move_goal_source *, uint32_t travel_flags,
                                        const qa_bot_vector_target *, bool *, qa_error *);
#endif
