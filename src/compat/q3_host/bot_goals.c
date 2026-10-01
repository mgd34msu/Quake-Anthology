#include "bot_records.h"
#include "qa/bot_runtime.h"

#include <stdio.h>

static bool state(q3_call *call, qa_bot_goals *goals, int32_t handle, bool freeing)
{
    if (handle > 0 && qa_bot_goals_has_handle(goals, (uint32_t)handle)) return true;
    char text[112];
    snprintf(text, sizeof(text), handle < 1 ? "goal state handle %d out of range\n" :
        freeing ? "invalid goal state handle %d\n" : "invalid goal state %d\n", handle);
    if (call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, text);
    return false;
}

typedef struct source_push { q3_call *call; uint8_t bytes[56]; } source_push;
static bool read_push(void *context, qa_bytes *goal, qa_error *error)
{
    source_push *source = context; q3_call *call = source->call;
    if (!q3_read(call, call->arguments[1], source->bytes, sizeof(source->bytes), error)) return false;
    int32_t entity = qa_load_i32le(source->bytes + 40);
    if (entity > 0 && !q3_bot_entity_number(call, entity, &entity, error)) return false;
    qa_store_u32le(source->bytes + 40, (uint32_t)entity);
    *goal = (qa_bytes){source->bytes, sizeof(source->bytes)}; return true;
}

static bool query(q3_call *call, qa_bot_goals *goals, int32_t *result, qa_error *error)
{
    qa_buffer text = {0}; qa_bot_goal goal; bool found = false, ok;
    uint64_t destination;
    if (call->service == 567) {
        destination = call->arguments[1];
        ok = q3_bot_goal_read(call, destination, &goal, error) &&
            qa_bot_goals_camp(goals, q3_integer(call, 0), &goal, result, error);
        found = ok && *result != 0;
    } else {
        bool location = call->service == 568;
        destination = call->arguments[location ? 1 : 2];
        ok = q3_string(call, call->arguments[location ? 0 : 1], &text, error) &&
             q3_bot_goal_read(call, destination, &goal, error);
        if (ok) ok = location ? qa_bot_goals_location(goals, (const char *)text.data, &goal, &found, error) :
            qa_bot_goals_level_item(goals, q3_integer(call, 0), (const char *)text.data, &goal, &found, error);
        if (ok && found) *result = location ? 1 : goal.number;
    }
    qa_buffer_free(&text);
    return ok && (!found || q3_bot_goal_fields(call, destination, &goal,
        call->service == 568 ? Q3_GOAL_LOCATION : call->service == 539 ? Q3_GOAL_LEVEL_ITEM : Q3_GOAL_FULL, error));
}

static bool choice(q3_call *call, qa_bot_goals *goals, int32_t *result, qa_error *error)
{
    qa_bot_goal_choice input = {.travel_flags = (uint32_t)q3_integer(call, 3), .nearby = call->service == 536};
    qa_bot_goal long_term;
    if (!q3_vector(call, call->arguments[1], &input.origin, error)) return false;
    if (input.nearby) {
        input.maximum_time = q3_float(call, 5);
        if (call->arguments[4]) {
            if (!q3_bot_goal_read(call, call->arguments[4], &long_term, error)) return false;
            input.long_term = &long_term;
        }
    }
    int32_t handle = q3_integer(call, 0);
    if (!state(call, goals, handle, false)) return true;
    q3_bot_memory memory = {call, call->arguments[2]};
    qa_bot_inventory_view inventory = {.context = &memory, .read = q3_bot_inventory_read};
    input.inventory_source = &inventory;
    bool selected;
    if (!qa_bot_goals_choose(goals, (uint32_t)handle, &input, &selected, error)) return false;
    *result = selected; return true;
}

q3_service_result q3_bot_goals(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME ||
        !((call->service >= 525 && call->service <= 547) ||
          (call->service >= 565 && call->service <= 568) || call->service == 571 || call->service == 573))
        return Q3_UNHANDLED;
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    qa_bot_goals *goals = qa_bot_runtime_goals(runtime);
    if (!goals) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot goal owner is unbound"); return Q3_FAILED; }
    bool ok = true, found; qa_bot_goal goal; uint32_t handle = (uint32_t)q3_integer(call, 0);
    switch (call->service) {
    case 535: case 536: ok = choice(call, goals, result, error); break;
    case 537: {
        qa_vec3 origin;
        ok = q3_vector(call, call->arguments[0], &origin, error) &&
             q3_bot_goal_read(call, call->arguments[1], &goal, error);
        if (ok) *result = qa_bot_goal_touching(origin, &goal);
        break;
    }
    case 538: {
        qa_vec3 eye, angles;
        int32_t client;
        if(!q3_bot_client_number(call,q3_integer(call,0),&client,error)) {ok=false;break;}
        ok = q3_vector(call, call->arguments[1], &eye, error) && q3_vector(call, call->arguments[2], &angles, error) &&
             q3_bot_goal_read(call, call->arguments[3], &goal, error) &&
             qa_bot_goals_missing_visible(goals, client, eye, &goal, &found, error);
        if (ok) *result = found;
        break;
    }
    case 539: case 567: case 568: ok = query(call, goals, result, error); break;
    case 541: ok = qa_bot_runtime_init_level_items(runtime, error); break;
    case 542: ok = qa_bot_goals_update_items(goals, qa_bot_runtime_navigation(runtime, -1), error); break;
    case 543: {
        *result = 9;
        if (!state(call, goals, (int32_t)handle, false)) break;
        qa_buffer path = {0};
        ok = q3_string(call, call->arguments[1], &path, error) &&
            qa_bot_runtime_goal_weights(runtime, handle, (const char *)path.data, result, error);
        qa_buffer_free(&path);
        break;
    }
    case 532: ok = q3_write_string(call, call->arguments[1], qa_bot_goals_name(goals, q3_integer(call, 0)),
                                    q3_integer(call, 2), error); break;
    case 546: {
        int32_t client;
        ok = q3_bot_client_number(call,q3_integer(call,0),&client,error) &&
             qa_bot_goals_allocate(goals, client, &handle, error);
        if (ok) *result = (int32_t)handle;
        break;
    }
    case 565: {
        bool first = state(call, goals, q3_integer(call, 0), false);
        bool second = state(call, goals, q3_integer(call, 1), false);
        bool child = state(call, goals, q3_integer(call, 2), false);
        if (first && second && child)
            ok = qa_bot_goals_interbreed(goals, handle, (uint32_t)q3_integer(call, 1),
                                         (uint32_t)q3_integer(call, 2), &found, error);
        break;
    }
    case 545: {
        qa_buffer ignored = {0};
        ok = q3_string(call, call->arguments[1], &ignored, error);
        qa_buffer_free(&ignored);
        if (ok && state(call, goals, (int32_t)handle, false)) ok = qa_bot_goals_save_weights(goals, handle, error);
        break;
    }
    default:
        if (!state(call, goals, (int32_t)handle, call->service == 547)) break;
        switch (call->service) {
        case 525: ok = qa_bot_goals_reset(goals, handle, error); break;
        case 526: ok = qa_bot_goals_avoid_clear(goals, handle, error); break;
        case 527: {
            source_push source = {.call = call};
            ok = qa_bot_goals_push_source_from(goals, handle, &source, read_push, &found, error); break;
        }
        case 528: ok = qa_bot_goals_pop(goals, handle, error); break;
        case 529: ok = qa_bot_goals_empty(goals, handle, error); break;
        case 530: ok = qa_bot_goals_dump_avoid(goals, handle, error); break;
        case 531: ok = qa_bot_goals_dump_stack(goals, handle, error); break;
        case 533: case 534: {
            qa_bytes source = {0};
            ok = qa_bot_goals_top_source(goals, handle, call->service == 534, &source, &found, error);
            if (ok && found) {
                uint8_t bytes[56];
                if (source.size != sizeof(bytes) || !source.data) {
                    ok = q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 goal source lacks its actual 56-byte record"); break;
                }
                memcpy(bytes, source.data, sizeof(bytes));
                int32_t entity = qa_load_i32le(bytes + 40);
                ok = !entity || q3_bot_source_entity(call, entity, &entity, error);
                if (ok) {
                    qa_store_u32le(bytes + 40, (uint32_t)entity);
                    ok = q3_write(call, call->arguments[1], (qa_bytes){bytes, sizeof(bytes)}, error);
                }
                if (ok) *result = 1;
            }
            break;
        }
        case 540: {
            float time;
            ok = qa_bot_goals_avoid_time(goals, handle, q3_integer(call, 1), &time, error);
            if (ok) *result = q3_float_bits(time);
            break;
        }
        case 544: ok = qa_bot_goals_weights(goals, handle, NULL, error); break;
        case 547: ok = qa_bot_goals_free(goals, handle, error); break;
        case 566: ok = qa_bot_goals_mutate(goals, handle, error); break;
        case 571: ok = qa_bot_goals_avoid_remove(goals, handle, q3_integer(call, 1), error); break;
        case 573: ok = qa_bot_goals_avoid_set(goals, handle, q3_integer(call, 1), q3_float(call, 2), error); break;
        default: return Q3_UNHANDLED;
        }
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
