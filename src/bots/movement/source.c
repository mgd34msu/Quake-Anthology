#include "internal.h"

qa_bot_vector_source bot_goal_origin(const qa_bot_move_goal_source *goal) {
    return goal->value ? (qa_bot_vector_source){.value = &goal->value->origin} : goal->origin;
}
bool bot_goal_area(const qa_bot_move_goal_source *goal, uint32_t *out, qa_error *e) {
    int32_t value;
    if (goal->value) value = goal->value->area;
    else if (!goal->area(goal->context, &value, e)) return false;
    *out = (uint32_t)value;
    return true;
}
bool bot_result_read(const qa_bot_move_result_io *io, qa_bot_move_result_field field,
                     int32_t *out, qa_error *e) {
    if (!io->value) return io->read(io->context, field, out, e);
    const qa_bot_move_result *v = io->value;
    switch (field) {
    case QA_BOT_RESULT_FAILURE: *out = v->failure; break;
    case QA_BOT_RESULT_TYPE: *out = v->type; break;
    case QA_BOT_RESULT_BLOCKED: *out = v->blocked; break;
    case QA_BOT_RESULT_BLOCK_ENTITY: *out = v->block_entity; break;
    case QA_BOT_RESULT_TRAVEL_TYPE: *out = v->travel_type; break;
    case QA_BOT_RESULT_FLAGS: memcpy(out, &v->flags, sizeof(*out)); break;
    case QA_BOT_RESULT_WEAPON: *out = v->weapon; break;
    }
    return true;
}
bool bot_result_write(const qa_bot_move_result_io *io, qa_bot_move_result_field field,
                      int32_t value, qa_error *e) {
    if (!io->value) return io->write(io->context, field, value, e);
    qa_bot_move_result *v = io->value;
    switch (field) {
    case QA_BOT_RESULT_FAILURE: v->failure = value != 0; break;
    case QA_BOT_RESULT_TYPE: v->type = value; break;
    case QA_BOT_RESULT_BLOCKED: v->blocked = value != 0; break;
    case QA_BOT_RESULT_BLOCK_ENTITY: v->block_entity = value; break;
    case QA_BOT_RESULT_TRAVEL_TYPE: v->travel_type = value; break;
    case QA_BOT_RESULT_FLAGS: v->flags = (uint32_t)value; break;
    case QA_BOT_RESULT_WEAPON: v->weapon = value; break;
    }
    return true;
}
bool bot_result_flags(const qa_bot_move_result_io *io, uint32_t flags, qa_error *e) {
    int32_t value;
    if (!bot_result_read(io, QA_BOT_RESULT_FLAGS, &value, e)) return false;
    uint32_t bits = (uint32_t)value | flags;
    memcpy(&value, &bits, sizeof(value));
    return bot_result_write(io, QA_BOT_RESULT_FLAGS, value, e);
}
bool bot_result_clear(const qa_bot_move_result_io *io, qa_error *e) {
    for (qa_bot_move_result_field field = QA_BOT_RESULT_FAILURE;
         field <= QA_BOT_RESULT_FLAGS; ++field)
        if (!bot_result_write(io, field, 0, e)) return false;
    return true;
}
bool bot_result_copy(const qa_bot_move_result_io *io, const qa_bot_move_result *value,
                     qa_error *e) {
    if (io->value) { *io->value = *value; return true; }
    qa_bot_move_result copy = *value;
    qa_bot_move_result_io source = {.value = &copy};
    for (qa_bot_move_result_field field = QA_BOT_RESULT_FAILURE;
         field <= QA_BOT_RESULT_WEAPON; ++field) {
        int32_t word;
        if (!bot_result_read(&source, field, &word, e) ||
            !io->write(io->context, field, word, e)) return false;
    }
    for (qa_bot_move_result_vector field = QA_BOT_RESULT_DIRECTION;
         field <= QA_BOT_RESULT_VIEW; ++field) {
        qa_bot_vector_source vector = {.value = field == QA_BOT_RESULT_DIRECTION ?
            &copy.direction : &copy.ideal_view_angles};
        for (unsigned axis = 0; axis < 3; ++axis) {
            float component;
            if (!qa_bot_vector_component(&vector, axis, &component, e) ||
                !io->write_vector(io->context, field, axis, component, e)) return false;
        }
    }
    return true;
}
