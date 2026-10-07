#include "bot_records.h"
#include "qa/bot_movement_source.h"
#include "qa/bot_navigation_source.h"
#include "qa/bot_runtime.h"

#include <stdio.h>

static bool field_address(const q3_bot_memory *memory, uint32_t offset,
                           uint64_t *out, qa_error *error)
{
    if (!memory->address || memory->address > UINT64_MAX - offset)
        return q3_fail(error, QA_ERROR_ARGUMENT, offset, "Q3 bot movement field exceeds allocation");
    *out = memory->address + offset;
    return true;
}

static bool read_integer(q3_bot_memory *memory, uint32_t offset, int32_t *out, qa_error *error)
{
    uint64_t address; uint8_t bytes[4];
    if (!field_address(memory, offset, &address, error) ||
        !q3_read(memory->call, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return true;
}

static bool read_float(q3_bot_memory *memory, uint32_t offset, float *out, qa_error *error)
{
    int32_t bits;
    if (!read_integer(memory, offset, &bits, error)) return false;
    memcpy(out, &bits, sizeof(*out)); return true;
}

static bool write_integer(q3_bot_memory *memory, uint32_t offset, int32_t value, qa_error *error)
{
    uint64_t address;
    return field_address(memory, offset, &address, error) &&
        q3_write_word(memory->call, address, (uint32_t)value, error);
}

static bool goal_area(void *context, int32_t *out, qa_error *error)
{
    return read_integer(context, 12, out, error);
}

static qa_bot_move_goal_source goal_source(q3_bot_memory *memory)
{
    return (qa_bot_move_goal_source){.context = memory, .area = goal_area,
        .origin = {.context = memory, .read = q3_bot_vector_read}};
}

static bool result_read(void *context, qa_bot_move_result_field field,
                          int32_t *out, qa_error *error)
{
    return read_integer(context, (uint32_t)field * 4u, out, error);
}

static bool result_write(void *context, qa_bot_move_result_field field,
                           int32_t value, qa_error *error)
{
    q3_bot_memory *memory=context;
    if(field==QA_BOT_RESULT_BLOCK_ENTITY && value &&
       !q3_bot_source_entity(memory->call,value,&value,error)) return false;
    return write_integer(context, (uint32_t)field * 4u, value, error);
}

static bool result_vector(void *context, qa_bot_move_result_vector field,
                            unsigned component, float value, qa_error *error)
{
    return write_integer(context, (field == QA_BOT_RESULT_DIRECTION ? 28u : 40u) + component * 4u,
        q3_float_bits(value), error);
}

static bool init_integer(void *context, qa_bot_move_init_field field, int32_t *out, qa_error *error)
{
    static const uint32_t offsets[] = {36, 40, 48, 64};
    if(!read_integer(context, offsets[field], out, error)) return false;
    q3_bot_memory *memory=context;
    if(field==QA_BOT_INIT_ENTITY) return q3_bot_entity_number(memory->call,*out,out,error);
    if(field==QA_BOT_INIT_CLIENT) return q3_bot_client_number(memory->call,*out,out,error);
    return true;
}

static bool init_vector(void *context, qa_bot_move_init_vector field, unsigned component,
                          float *out, qa_error *error)
{
    static const uint32_t offsets[] = {0, 12, 24, 52};
    return read_float(context, offsets[field] + component * 4u, out, error);
}

static bool init_time(void *context, float *out, qa_error *error)
{
    return read_float(context, 44, out, error);
}

static bool state(q3_call *call, qa_bot_moves *moves, int32_t handle)
{
    if (handle > 0 && qa_bot_moves_has_handle(moves, (uint32_t)handle)) return true;
    char text[112];
    snprintf(text, sizeof(text), handle < 1 ?
        "move state handle %d out of range\n" : "invalid move state %d\n", handle);
    if (call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, text);
    return false;
}

static bool move_to_goal(q3_call *call, qa_bot_moves *moves, qa_error *error)
{
    q3_bot_memory target = {call, call->arguments[0]}, goal = {call, call->arguments[2]};
    qa_bot_move_result_io result = {.context = &target, .read = result_read,
        .write = result_write, .write_vector = result_vector};
    for (qa_bot_move_result_field field = QA_BOT_RESULT_FAILURE; field <= QA_BOT_RESULT_FLAGS; ++field)
        if (!result_write(&target, field, 0, error)) return false;
    if (!goal.address || !state(call, moves, q3_integer(call, 1)))
        return result_write(&target, QA_BOT_RESULT_FAILURE, 1, error);
    qa_bot_move_goal_source source = goal_source(&goal);
    return qa_bot_moves_goal_from(moves, (uint32_t)q3_integer(call, 1), &source,
        (uint32_t)q3_integer(call, 3), &result, error);
}

q3_service_result q3_bot_movement(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME ||
        !((call->service >= 548 && call->service <= 557) ||
          call->service == 572 || call->service == 574)) return Q3_UNHANDLED;
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    qa_bot_moves *moves = qa_bot_runtime_moves(runtime);
    if (!moves) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot movement owner is unbound"); return Q3_FAILED;
    }
    uint32_t handle = (uint32_t)q3_integer(call, 0);
    bool ok = true, found = false;
    if (call->service == 549) ok = move_to_goal(call, moves, error);
    else if (call->service == 555) {
        ok = qa_bot_moves_allocate(moves, &handle, error);
        if (ok) *result = (int32_t)handle;
    } else if (call->service == 553) {
        uint32_t area = 0; qa_actor_id pass = {0};
        int32_t number = q3_integer(call, 1);
        if (call->host->game && number >= 0 && number < 1022) {
            qa_actor_id actor = call->host->game->slots[number].actor;
            if (qa_actors_get(qa_session_actors(call->host->options.session), actor)) pass = actor;
        }
        q3_bot_memory memory = {call, call->arguments[0]};
        qa_bot_vector_source origin = {.context = &memory, .read = q3_bot_vector_read};
        qa_bot_navigation *navigation = qa_bot_runtime_navigation(runtime, -1);
        ok = qa_bot_navigation_reachable_from(navigation, &origin, pass, &area, error);
        if (ok) *result = (int32_t)area;
    } else if (call->service == 572) {
        if (!call->arguments[2]) return Q3_COMPLETED;
        q3_bot_memory origin = {call, call->arguments[0]}, goal = {call, call->arguments[2]},
                      destination = {call, call->arguments[4]};
        qa_bot_vector_source start = {.context = &origin, .read = q3_bot_vector_read};
        qa_bot_move_goal_source source = goal_source(&goal);
        qa_bot_vector_target target = {.context = &destination, .admit = q3_bot_vector_admit,
            .write = q3_bot_vector_write};
        ok = qa_bot_moves_visible_position_from(moves, &start, (uint32_t)q3_integer(call, 1),
            &source, (uint32_t)q3_integer(call, 3), &target, &found, error);
        if (ok) *result = found;
    } else {
        if (call->service == 554 && !call->arguments[1]) return Q3_COMPLETED;
        if (!state(call, moves, (int32_t)handle)) return Q3_COMPLETED;
        q3_bot_memory memory = {call, call->arguments[1]};
        qa_bot_vector_source vector = {.context = &memory, .read = q3_bot_vector_read};
        switch (call->service) {
        case 548: ok = qa_bot_moves_reset(moves, handle, error); break;
        case 550:
            ok = qa_bot_moves_direction_from(moves, handle, &vector, q3_float(call, 2),
                (uint32_t)q3_integer(call, 3), &found, error);
            if (ok) *result = found;
            break;
        case 551: case 552: ok = qa_bot_moves_reset_avoid(moves, handle, call->service == 552, error); break;
        case 554: {
            qa_bot_move_goal_source source = goal_source(&memory);
            q3_bot_memory destination = {call, call->arguments[4]};
            qa_bot_vector_target target = {.context = &destination, .admit = q3_bot_vector_admit,
                .write = q3_bot_vector_write};
            ok = qa_bot_moves_view_target_from(moves, handle, &source, (uint32_t)q3_integer(call, 2),
                q3_float(call, 3), &target, &found, error);
            if (ok) *result = found;
            break;
        }
        case 556: ok = qa_bot_moves_free(moves, handle, error); break;
        case 557: {
            qa_bot_move_init_source source = {.context = &memory, .integer = init_integer,
                .vector = init_vector, .think_time = init_time};
            ok = qa_bot_moves_initialize_from(moves, handle, &source, error);
            break;
        }
        case 574: ok = qa_bot_moves_avoid_spot_from(moves, handle, &vector,
            q3_float(call, 2), q3_integer(call, 3), error); break;
        default: return Q3_UNHANDLED;
        }
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
