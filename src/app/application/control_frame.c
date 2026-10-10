#include "qa/input.h"
#include "control_frame.h"
#include "native_q3_control.h"
#include "guest_input_private.h"
#include "guest_q3_components.h"
#include "guest_q3_save.h"
#include "guest_qc_internal.h"
#include "bots_round.h"
#include "native_q3_clients.h"
#include "native_q3_wire_state.h"
#include "guest_native_q2_input.h"
#include "qa/game_q3_source.h"
#include "qa/game_q2_wire.h"
#include "qa/network_unified_session.h"
#include "qa/text.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum control_command_domain {
    CONTROL_COMMAND_SELECTED,
    CONTROL_COMMAND_QW_SOURCE,
    CONTROL_COMMAND_Q3_SOURCE,
    CONTROL_COMMAND_NQ_SOURCE,
    CONTROL_COMMAND_Q2_SOURCE,
    CONTROL_COMMAND_UNIFIED
} control_command_domain;

typedef struct control_unified {
    qa_unified_movement movement;
    uint64_t sequence;
    qa_actor_owner arsenal;
    qa_item_id weapon;
    bool has_arsenal, use_holdable, has_impulse;
    uint8_t impulse;
} control_unified;

typedef struct control_input {
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_movement_command latest;
    control_unified unified;
    uint64_t sequence;
    control_command_domain domain;
    uint64_t source_time_ns, accepted_time_ns;
    uint64_t qw_receipt_time_ns;
    bool has_body_base;
    qa_bounds body_base;
    qa_actor_owner arsenal;
    qa_item_id weapon;
    uint8_t impulse;
    bool seen, retained;
    uint64_t owned_frame_number;
    bool frame_owned;
    qa_actor_id source_turn_actor;
    qa_actor_owner source_turn_provider;
    uint64_t source_turn_frame_number;
    qa_actor_id q1_source_actor;
    qa_actor_owner q1_source_provider;
    uint64_t q1_source_frame_number, q1_source_sequence;
    bool q1_source_deferred;
    struct application_control_turn *turn;
} control_input;
typedef struct control_input_slot {
    control_input value;
    struct control_input_slot *next;
    bool listed;
} control_input_slot;
typedef struct control_group {
    struct control_group *next;
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_actor_owner arsenal;
    qa_item_id weapon;
    size_t count;
    bool quakeworld;
    bool before_source;
    bool bot;
    control_command_domain domain;
    uint64_t source_time_ns;
    control_unified unified;
    qa_movement_command commands[];
} control_group;
struct application_control_frames {
    qa_application *application;
    control_input_slot *inputs, *first_input;
    uint32_t capacity;
    control_group *head, *tail;
    const application_control_context *current;
    qa_movement_state *state;
    qa_actor_id state_actor;
    const qa_movement_call *call;
    const control_group *q3_group;
    struct application_control_mod_input *mod_inputs;
    qa_actor_id *touched;
    size_t touched_count, touched_capacity;
    bool draining;
};

static control_input *input_enroll(struct application_control_frames *owner, uint32_t index)
{
    control_input_slot *slot = &owner->inputs[index];
    if (!slot->listed) {
        control_input_slot **link = &owner->first_input;
        while (*link && *link < slot) link = &(*link)->next;
        slot->next = *link; *link = slot; slot->listed = true;
    }
    return &slot->value;
}

static void inputs_prune(struct application_control_frames *owner)
{
    control_input_slot **link = &owner->first_input;
    while (*link) {
        control_input_slot *slot = *link;
        const control_input *input = &slot->value;
        if (input->actor.registry || input->turn || input->source_turn_actor.registry ||
            input->q1_source_actor.registry || input->frame_owned) link = &slot->next;
        else { *link = slot->next; slot->next = NULL; slot->listed = false; }
    }
}

static int32_t command_word(uint32_t bits)
{
    int32_t value; memcpy(&value,&bits,sizeof(value)); return value;
}

static bool command_integer(double value,double minimum,double maximum)
{ return isfinite(value) && value==trunc(value) && value>=minimum && value<=maximum; }

static bool command_valid(const qa_movement_command *command)
{
    return (unsigned)command->kind <= QA_RULESET_Q3 && qa_vec_finite(command->angles) &&
        isfinite(command->forward_move) && isfinite(command->side_move) &&
        isfinite(command->up_move) && isfinite(command->acknowledged_server_seconds);
}

static size_t unified_numbers(qa_unified_movement *movement, double **fields)
{
    size_t count = 0;
#define NUMBER(field) fields[count++] = &(field)
#define VECTOR(field) NUMBER((field).x); NUMBER((field).y); NUMBER((field).z)
    switch (movement->kind) {
    case QA_RULESET_NETQUAKE:
        NUMBER(movement->data.nq.acknowledged_seconds); VECTOR(movement->data.nq.angles);
        NUMBER(movement->data.nq.forward); NUMBER(movement->data.nq.side); NUMBER(movement->data.nq.up);
        NUMBER(movement->data.nq.buttons); NUMBER(movement->data.nq.impulse); break;
    case QA_RULESET_QUAKEWORLD:
        NUMBER(movement->data.qw.milliseconds); VECTOR(movement->data.qw.angles);
        NUMBER(movement->data.qw.forward); NUMBER(movement->data.qw.side); NUMBER(movement->data.qw.up);
        NUMBER(movement->data.qw.buttons); NUMBER(movement->data.qw.impulse); break;
    case QA_RULESET_Q2_CLASSIC:
        NUMBER(movement->data.q2.milliseconds);
        for (unsigned i = 0; i < 3; ++i) NUMBER(movement->data.q2.angle_shorts[i]);
        NUMBER(movement->data.q2.forward); NUMBER(movement->data.q2.side); NUMBER(movement->data.q2.up);
        NUMBER(movement->data.q2.buttons); NUMBER(movement->data.q2.impulse); NUMBER(movement->data.q2.light_level); break;
    case QA_RULESET_Q2_RERELEASE:
        NUMBER(movement->data.q2r.milliseconds); VECTOR(movement->data.q2r.angles);
        NUMBER(movement->data.q2r.forward); NUMBER(movement->data.q2r.side);
        NUMBER(movement->data.q2r.buttons); NUMBER(movement->data.q2r.server_frame); break;
    case QA_RULESET_Q3:
        NUMBER(movement->data.q3.server_time_ms);
        for (unsigned i = 0; i < 3; ++i) NUMBER(movement->data.q3.angle_words[i]);
        NUMBER(movement->data.q3.buttons); NUMBER(movement->data.q3.weapon);
        NUMBER(movement->data.q3.forward); NUMBER(movement->data.q3.right); NUMBER(movement->data.q3.up); break;
    }
#undef VECTOR
#undef NUMBER
    return count;
}

static bool unified_movement_valid(const qa_unified_movement *raw)
{
    if ((unsigned)raw->kind>QA_RULESET_Q3) return false;
    qa_unified_movement checked=*raw; double *fields[10];
    size_t count=unified_numbers(&checked,fields);
    for (size_t i=0;i<count;++i) if (!isfinite(*fields[i])) return false;
    double buttons,impulse=0,milliseconds=0,axes[3]; const double *words=NULL;
    qa_unified_vec3 angles={0};
    switch (raw->kind) {
    case QA_RULESET_NETQUAKE:
        buttons=raw->data.nq.buttons; impulse=raw->data.nq.impulse;
        axes[0]=raw->data.nq.forward; axes[1]=raw->data.nq.side; axes[2]=raw->data.nq.up;
        angles=raw->data.nq.angles; break;
    case QA_RULESET_QUAKEWORLD:
        buttons=raw->data.qw.buttons; impulse=raw->data.qw.impulse;
        milliseconds=raw->data.qw.milliseconds;
        axes[0]=raw->data.qw.forward; axes[1]=raw->data.qw.side; axes[2]=raw->data.qw.up;
        angles=raw->data.qw.angles; break;
    case QA_RULESET_Q2_CLASSIC:
        buttons=raw->data.q2.buttons; impulse=raw->data.q2.impulse;
        milliseconds=raw->data.q2.milliseconds; words=raw->data.q2.angle_shorts;
        axes[0]=raw->data.q2.forward; axes[1]=raw->data.q2.side; axes[2]=raw->data.q2.up;
        if (!command_integer(raw->data.q2.light_level,0,UINT8_MAX)) return false;
        break;
    case QA_RULESET_Q2_RERELEASE:
        buttons=raw->data.q2r.buttons; milliseconds=raw->data.q2r.milliseconds;
        axes[0]=raw->data.q2r.forward; axes[1]=raw->data.q2r.side; axes[2]=0;
        angles=raw->data.q2r.angles;
        if (!command_integer(raw->data.q2r.server_frame,-1,INT32_MAX)) return false;
        break;
    case QA_RULESET_Q3:
        buttons=raw->data.q3.buttons; words=raw->data.q3.angle_words;
        axes[0]=raw->data.q3.forward; axes[1]=raw->data.q3.right; axes[2]=raw->data.q3.up;
        if (!command_integer(raw->data.q3.server_time_ms,INT32_MIN,INT32_MAX) ||
            !command_integer(raw->data.q3.weapon,0,UINT8_MAX)) return false;
        break;
    default: return false;
    }
    if (!command_integer(buttons,0,UINT32_MAX) || !command_integer(impulse,0,UINT8_MAX) ||
        !command_integer(milliseconds,0,raw->kind==QA_RULESET_Q2_RERELEASE?1000:UINT8_MAX)) return false;
    for (unsigned i=0;words && i<3;++i)
        if (!command_integer(words[i],INT32_MIN,INT32_MAX)) return false;
    for (unsigned i=0;i<3;++i)
        if (raw->kind==QA_RULESET_Q2_RERELEASE ? fabs(axes[i])>FLT_MAX :
            !command_integer(axes[i],INT16_MIN,INT16_MAX)) return false;
    if (fabs(angles.x)>FLT_MAX || fabs(angles.y)>FLT_MAX || fabs(angles.z)>FLT_MAX) return false;
    return true;
}

static bool unified_valid(const control_unified *receipt)
{
    if ((unsigned)receipt->movement.kind > QA_RULESET_Q3 || receipt->sequence > QA_UNIFIED_SAFE_INTEGER ||
        (!receipt->has_arsenal && (receipt->arsenal || receipt->weapon || receipt->use_holdable ||
            receipt->has_impulse || receipt->impulse)) ||
        (receipt->has_arsenal && !receipt->arsenal) || (!receipt->has_impulse && receipt->impulse)) return false;
    return unified_movement_valid(&receipt->movement);
}

static bool unified_equal(const control_unified *a, const control_unified *b)
{
    if (a->movement.kind != b->movement.kind || a->sequence != b->sequence ||
        a->arsenal != b->arsenal || a->weapon != b->weapon || a->has_arsenal != b->has_arsenal ||
        a->use_holdable != b->use_holdable || a->has_impulse != b->has_impulse || a->impulse != b->impulse) return false;
    qa_unified_movement left = a->movement, right = b->movement;
    double *lf[10], *rf[10]; size_t count = unified_numbers(&left, lf);
    if (unified_numbers(&right, rf) != count) return false;
    for (size_t i = 0; i < count; ++i) if (memcmp(lf[i], rf[i], sizeof(double))) return false;
    return true;
}

bool qa_application_control_project_unified(const qa_unified_movement *raw,
    const qa_movement_state *state, uint64_t sequence, qa_movement_command *out, qa_error *error)
{
    if (!raw || !state || !out || (unsigned)raw->kind > QA_RULESET_Q3 || raw->kind != state->kind ||
        sequence > UINT64_C(9007199254740991))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified receipt lost its actual selected movement");
    if (!unified_movement_valid(raw))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified movement exceeds its native command fields");
    qa_movement_command command = {.kind = raw->kind, .sequence = sequence};
    double forward = 0, side = 0, up = 0, buttons = 0, impulse = 0, milliseconds = 0;
    qa_unified_vec3 angles = {0};
    switch (raw->kind) {
    case QA_RULESET_NETQUAKE:
        command.acknowledged_server_seconds = raw->data.nq.acknowledged_seconds;
        angles = raw->data.nq.angles; forward = raw->data.nq.forward; side = raw->data.nq.side;
        up = raw->data.nq.up; buttons = raw->data.nq.buttons; impulse = raw->data.nq.impulse; break;
    case QA_RULESET_QUAKEWORLD:
        milliseconds = raw->data.qw.milliseconds; angles = raw->data.qw.angles;
        forward = raw->data.qw.forward; side = raw->data.qw.side; up = raw->data.qw.up;
        buttons = raw->data.qw.buttons; impulse = raw->data.qw.impulse; break;
    case QA_RULESET_Q2_CLASSIC:
        milliseconds = raw->data.q2.milliseconds;
        for (unsigned i = 0; i < 3; ++i)
            command.angle_words[i] = command_word((uint32_t)(int32_t)raw->data.q2.angle_shorts[i] - (uint32_t)(int32_t)state->data.q2.delta_angle_shorts[i]);
        forward = raw->data.q2.forward; side = raw->data.q2.side; up = raw->data.q2.up;
        buttons = raw->data.q2.buttons; impulse = raw->data.q2.impulse;
        command.light_level = (uint8_t)raw->data.q2.light_level; break;
    case QA_RULESET_Q2_RERELEASE:
        milliseconds = raw->data.q2r.milliseconds;
        angles = (qa_unified_vec3){raw->data.q2r.angles.x - state->data.q2r.delta_angles.x,
            raw->data.q2r.angles.y - state->data.q2r.delta_angles.y,
            raw->data.q2r.angles.z - state->data.q2r.delta_angles.z};
        forward = raw->data.q2r.forward; side = raw->data.q2r.side; buttons = raw->data.q2r.buttons;
        command.server_frame = (int32_t)raw->data.q2r.server_frame; break;
    case QA_RULESET_Q3:
        command.server_time_ms = (int32_t)raw->data.q3.server_time_ms;
        {
            int64_t elapsed = (int64_t)command.server_time_ms - state->data.q3.command_time_ms;
            milliseconds = elapsed <= 0 ? 0 : elapsed > 200 ? 200 : (double)elapsed;
        }
        for (unsigned i = 0; i < 3; ++i)
            command.angle_words[i] = command_word((uint32_t)(int32_t)raw->data.q3.angle_words[i] - (uint32_t)state->data.q3.delta_angle_words[i]);
        command.weapon = (uint8_t)raw->data.q3.weapon;
        forward = raw->data.q3.forward; side = raw->data.q3.right; up = raw->data.q3.up;
        buttons = raw->data.q3.buttons; break;
    }
    if (milliseconds < 0 || milliseconds > UINT32_MAX ||
        fabs(forward) > FLT_MAX || fabs(side) > FLT_MAX || fabs(up) > FLT_MAX ||
        fabs(angles.x) > FLT_MAX || fabs(angles.y) > FLT_MAX || fabs(angles.z) > FLT_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified command exceeds its selected native field domain");
    command.milliseconds = (uint32_t)trunc(milliseconds);
    command.angles = qa_v3((float)angles.x, (float)angles.y, (float)angles.z);
    command.forward_move = (float)forward; command.side_move = (float)side; command.up_move = (float)up;
    command.buttons = (uint32_t)buttons; command.impulse = (uint8_t)impulse;
    *out = command;
    return true;
}

static bool unified_command(qa_application *app, qa_actor_id actor, const control_unified *receipt,
    qa_movement_command *out, qa_error *error)
{
    if (!app || !unified_valid(receipt) || actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified receipt lost its actual selected movement");
    return qa_application_control_project_unified(&receipt->movement, &app->controls[actor.slot].player.state,
        receipt->sequence, out, error);
}

static void unified_context(application_control_context *context, const control_unified *receipt)
{
    context->unified_command = true;
    context->unified_intent = receipt->has_arsenal;
    context->arsenal = receipt->arsenal; context->weapon = receipt->weapon;
    context->unified_holdable = receipt->use_holdable;
    context->unified_has_impulse = receipt->has_impulse;
    context->unified_impulse = receipt->impulse;
}

static bool q2_raw_valid(const qa_movement_command *command)
{
    if (!command_valid(command) || command->milliseconds > UINT8_MAX ||
        command->buttons > UINT8_MAX || command->weapon || command->server_time_ms ||
        command->acknowledged_server_seconds != 0) return false;
    if (command->kind == QA_RULESET_Q2_RERELEASE)
        return command->up_move == 0;
    if (command->kind != QA_RULESET_Q2_CLASSIC || command->server_frame ||
        command->forward_move < INT16_MIN || command->forward_move > INT16_MAX ||
        command->side_move < INT16_MIN || command->side_move > INT16_MAX ||
        command->up_move < INT16_MIN || command->up_move > INT16_MAX ||
        truncf(command->forward_move) != command->forward_move ||
        truncf(command->side_move) != command->side_move ||
        truncf(command->up_move) != command->up_move) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (command->angle_words[i] < INT16_MIN || command->angle_words[i] > INT16_MAX) return false;
    return true;
}

static qa_movement_command q3_command(const qa_q3_usercmd *raw, uint64_t sequence)
{
    qa_movement_command command = {.kind = QA_RULESET_Q3, .sequence = sequence,
        .server_time_ms = raw->serverTime, .buttons = (uint32_t)raw->buttons, .weapon = raw->weapon,
        .forward_move = raw->forwardmove, .side_move = raw->rightmove, .up_move = raw->upmove};
    memcpy(command.angle_words, raw->angles, sizeof(command.angle_words));
    return command;
}

static qa_q3_usercmd q3_source_command(const qa_movement_command *command, bool raw)
{
    qa_movement_command converted;
    qa_input_command_basis basis = {.kind = QA_RULESET_Q3, .words = true};
    qa_input_command_convert(command, NULL, &basis, &basis,
        raw ? (qa_input_axis_rule){0} : (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_NEAREST,
            .clamp = true, .minimum = -127, .maximum = 127, .float_product = true}, &converted);
    qa_q3_usercmd out = {.serverTime = command->server_time_ms,
        .buttons = (int32_t)command->buttons, .weapon = command->weapon,
        .forwardmove = (int8_t)converted.forward_move,
        .rightmove = (int8_t)converted.side_move, .upmove = (int8_t)converted.up_move};
    memcpy(out.angles, command->angle_words, sizeof(out.angles));
    return out;
}

static bool q3_raw_valid(const qa_movement_command *command)
{
    return command_valid(command) && command->kind == QA_RULESET_Q3 &&
        command->milliseconds == 0 && command->server_frame == 0 &&
        command->acknowledged_server_seconds == 0 && command->angles.x == 0 &&
        command->angles.y == 0 && command->angles.z == 0 && !command->impulse && !command->light_level &&
        command->forward_move >= -128 && command->forward_move <= 127 &&
        command->side_move >= -128 && command->side_move <= 127 &&
        command->up_move >= -128 && command->up_move <= 127 &&
        truncf(command->forward_move) == command->forward_move &&
        truncf(command->side_move) == command->side_move && truncf(command->up_move) == command->up_move;
}

static bool unified_mod_command(const control_unified *receipt, const qa_q3_player *player,
    uint64_t time_ns, qa_q3_usercmd *out, qa_error *error)
{
    if (!unified_valid(receipt))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component accepted unified command is invalid");
    qa_unified_movement raw = receipt->movement;
    double forward, side, up, buttons;
    qa_movement_command input = {.kind = raw.kind}, converted;
    qa_input_command_basis from = {.kind = raw.kind}, to = {
        .kind = QA_RULESET_Q3, .words = true, .relative = true};
    memcpy(to.delta_words, player->deltaAngles, sizeof(to.delta_words));
    qa_q3_usercmd command = {.serverTime = command_word((uint32_t)(time_ns / UINT64_C(1000000))),
        .weapon = (uint8_t)player->weapon};
    if (raw.kind == QA_RULESET_Q3) {
        command.serverTime = (int32_t)raw.data.q3.server_time_ms;
        for (unsigned i = 0; i < 3; ++i) input.angle_words[i] = (int32_t)raw.data.q3.angle_words[i];
        from.words = true;
        forward = raw.data.q3.forward; side = raw.data.q3.right; up = raw.data.q3.up;
        buttons = raw.data.q3.buttons;
    } else if (raw.kind == QA_RULESET_Q2_CLASSIC) {
        for (unsigned i = 0; i < 3; ++i) input.angle_words[i] = (int32_t)raw.data.q2.angle_shorts[i];
        from.words = true;
        forward = raw.data.q2.forward; side = raw.data.q2.side; up = raw.data.q2.up;
        buttons = raw.data.q2.buttons;
    } else {
        qa_unified_vec3 aim;
        if (raw.kind == QA_RULESET_NETQUAKE) {
            aim = raw.data.nq.angles; forward = raw.data.nq.forward; side = raw.data.nq.side;
            up = raw.data.nq.up; buttons = raw.data.nq.buttons;
        } else if (raw.kind == QA_RULESET_QUAKEWORLD) {
            aim = raw.data.qw.angles; forward = raw.data.qw.forward; side = raw.data.qw.side;
            up = raw.data.qw.up; buttons = raw.data.qw.buttons;
        } else {
            aim = raw.data.q2r.angles; forward = raw.data.q2r.forward; side = raw.data.q2r.side;
            buttons = raw.data.q2r.buttons;
            up = (uint32_t)buttons & 8u ? 200 : (uint32_t)buttons & 16u ? -200 : 0;
        }
        input.angles = qa_v3((float)aim.x, (float)aim.y, (float)aim.z);
    }
    uint32_t word = (uint32_t)buttons;
    bool holdable = receipt->has_arsenal ? receipt->use_holdable : raw.kind == QA_RULESET_Q3 && (word & 4u);
    command.buttons = command_word((raw.kind == QA_RULESET_Q3 ? word & ~4u : word & 1u) | (holdable ? 4u : 0u));
    qa_input_move_intent moves = {forward, side, up};
    if ((raw.kind == QA_RULESET_NETQUAKE || raw.kind == QA_RULESET_QUAKEWORLD) && (word & 2u)) moves.z = 320;
    qa_input_axis_rule rule = raw.kind == QA_RULESET_Q3 ? (qa_input_axis_rule){0} :
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_TRUNCATE, .clamp = true, .minimum = -127, .maximum = 127};
    qa_input_command_convert(&input, &moves, &from, &to, rule, &converted);
    memcpy(command.angles, converted.angle_words, sizeof(command.angles));
    command.forwardmove = (int8_t)converted.forward_move;
    command.rightmove = (int8_t)converted.side_move; command.upmove = (int8_t)converted.up_move;
    *out = command; return true;
}

static bool unified_q3_source(qa_application *app, qa_actor_id actor, application_provider *provider,
    const control_unified *receipt, qa_q3_usercmd *out, qa_error *error)
{
    uint32_t slot; qa_q3_player player; qa_clock_state clock;
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !qa_q3_wire_player_read(provider->state.q3, slot, &player, error) ||
        !qa_session_clock(app->session, provider->owner, &clock)) return false;
    qa_q3_usercmd command;
    if (!unified_mod_command(receipt, &player, clock.frame.time_ns, &command, error)) return false;
    qa_unified_movement raw = receipt->movement;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state selected;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &selected))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Unified Source command lost its selected Q3 arsenal");
        command.weapon = receipt->has_arsenal || raw.kind != QA_RULESET_Q3
            ? (uint8_t)selected.weapon : (uint8_t)raw.data.q3.weapon;
        if (receipt->weapon) {
            bool found = false;
            for (unsigned i = 1; i < QA_Q3_WEAPON_COUNT; ++i)
                if (qa_q3_weapon_item(arsenal->state.q3, (qa_q3_weapon)i, false) == receipt->weapon) {
                    command.weapon = (uint8_t)i; found = true; break;
                }
            if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Unified item is not a selected Q3 weapon");
        }
    }
    *out = command; return true;
}

static bool nq_raw_valid(const qa_movement_command *command)
{
    return command_valid(command) && command->kind == QA_RULESET_NETQUAKE &&
        !command->milliseconds && !command->server_time_ms && !command->server_frame &&
        !command->angle_words[0] && !command->angle_words[1] && !command->angle_words[2] &&
        !command->light_level && !command->weapon && command->buttons <= UINT8_MAX &&
        fabs(command->acknowledged_server_seconds) <= FLT_MAX &&
        (double)(float)command->acknowledged_server_seconds == command->acknowledged_server_seconds &&
        command->forward_move >= INT16_MIN && command->forward_move <= INT16_MAX &&
        command->side_move >= INT16_MIN && command->side_move <= INT16_MAX &&
        command->up_move >= INT16_MIN && command->up_move <= INT16_MAX &&
        truncf(command->forward_move) == command->forward_move &&
        truncf(command->side_move) == command->side_move && truncf(command->up_move) == command->up_move;
}

static bool command_equal(const qa_movement_command *left, const qa_movement_command *right)
{
    return left->kind == right->kind && left->sequence == right->sequence &&
        left->milliseconds == right->milliseconds && left->server_time_ms == right->server_time_ms &&
        left->server_frame == right->server_frame &&
        !memcmp(&left->acknowledged_server_seconds, &right->acknowledged_server_seconds, sizeof(double)) &&
        !memcmp(&left->angles.x, &right->angles.x, sizeof(float)) &&
        !memcmp(&left->angles.y, &right->angles.y, sizeof(float)) &&
        !memcmp(&left->angles.z, &right->angles.z, sizeof(float)) &&
        !memcmp(left->angle_words, right->angle_words, sizeof(left->angle_words)) &&
        !memcmp(&left->forward_move, &right->forward_move, sizeof(float)) &&
        !memcmp(&left->side_move, &right->side_move, sizeof(float)) &&
        !memcmp(&left->up_move, &right->up_move, sizeof(float)) &&
        left->buttons == right->buttons && left->impulse == right->impulse &&
        left->light_level == right->light_level && left->weapon == right->weapon;
}

static application_provider *execution_provider(qa_application *app, qa_actor_id actor)
{
    qa_actor_owner owner;
    if (!qa_session_execution(app->session, actor, &owner)) return NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner) return app->providers[i];
    return NULL;
}

static application_provider *source_provider(qa_application *app, qa_actor_id actor)
{
    application_provider *map = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (map && map->constructed && map->attached && !map->close_pending &&
        map->component.command_actor &&
        map->component.command_actor(map->component.state, app->session, actor)) return map;
    return execution_provider(app, actor);
}

bool qa_application_control_source_read(qa_application *app, qa_actor_id actor,
    qa_actor_owner *owner, const qa_cvars **variables, qa_error *error)
{
    if (!app || !owner || !variables || app->state != QA_APPLICATION_RUNNING ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Control Source read needs its actual admitted actor");
    const application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    qa_console *console; qa_cvars *cvars;
    if (!record->active || record->retired || !qa_actor_id_equal(record->player.actor, actor) ||
        !provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !application_guest_console_at(provider, 0, &console, &cvars, NULL) || !cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Control Source lost its actual command recipient registry");
    *owner = provider->owner; *variables = cvars;
    return true;
}

static bool native_q2_raw_source_client(const application_provider *provider, qa_actor_id actor)
{
    return application_native_q2_source_client(provider, actor) ||
        (application_native_q2_declared_source_client(provider, actor) &&
         application_native_q2_declared_raw_capable(provider) &&
         application_provider_for(provider->application, actor, QA_ROLE_ARSENAL, NULL) == provider);
}

static bool saved_source_client(application_provider *map, qa_actor_id actor, qa_error *error)
{
    uint32_t slot; bool member = false;
    if (map && map->constructed && map->attached && !map->close_pending) {
        if (map->kind == APPLICATION_PROVIDER_Q1)
            member = qa_q1_native_client_slot_prepared(map->state.q1, actor, &slot, NULL);
        else if (map->kind == APPLICATION_PROVIDER_Q3)
            member = qa_q3_native_client_slot(map->state.q3, actor, &slot, NULL);
        else if (map->kind == APPLICATION_PROVIDER_Q2 || native_q2_raw_source_client(map, actor)) {
            member = map->component.command_actor &&
                map->component.command_actor(map->component.state, map->application->session, actor);
        }
        else if (map->kind == APPLICATION_PROVIDER_QVM ||
            (map->kind == APPLICATION_PROVIDER_NATIVE && map->component.clock.kind == QA_RULESET_Q3))
            member = application_q3_guest_actor_client(map, actor, &slot) ||
                application_guest_q3_save_actor_client(map, actor, &slot);
        else if (map->kind == APPLICATION_PROVIDER_QC) {
            if (!application_qc_control_source_client(map, actor, &member, error)) return false;
        }
    }
    return member;
}

static application_provider *saved_source_provider(qa_application *app, qa_actor_id actor, qa_error *error)
{
    application_provider *map = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (saved_source_client(map, actor, error)) return map;
    return execution_provider(app, actor);
}

static bool source_client(const application_provider *provider)
{
    return provider && (provider->kind == APPLICATION_PROVIDER_Q1 ||
        (provider->kind == APPLICATION_PROVIDER_QC && !provider->state.qc.qualified));
}

static bool original_q3(const application_provider *provider)
{
    return provider && (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->component.clock.kind == QA_RULESET_Q3));
}

static bool nq_selected_sources_ready(qa_application *app, qa_actor_id actor)
{
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (movement && movement->kind == APPLICATION_PROVIDER_NATIVE && movement->state.native.q2_engine)
        return movement == arsenal && movement->constructed && movement->attached && !movement->close_pending;
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_NATIVE && arsenal->state.native.q2_engine) return false;
    return application_arsenal_guest_stage_ready(app, actor);
}

static bool body_base_owner(const application_provider *source, qa_ruleset_id kind)
{
    return kind == QA_RULESET_NETQUAKE || (kind == QA_RULESET_QUAKEWORLD && source &&
        source->kind == APPLICATION_PROVIDER_QC && !source->state.qc.qualified &&
        source->component.clock.kind == QA_RULESET_QUAKEWORLD);
}

static bool bounds_equal(qa_bounds a, qa_bounds b)
{
    return a.mins.x == b.mins.x && a.mins.y == b.mins.y && a.mins.z == b.mins.z &&
        a.maxs.x == b.maxs.x && a.maxs.y == b.maxs.y && a.maxs.z == b.maxs.z;
}

bool application_control_body_request(qa_application *app, qa_actor_id actor, qa_bounds current,
    application_client_outputs *outputs, qa_error *error)
{
    if (!app || !outputs || !app->control_frames || actor.slot >= app->control_capacity ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Body request needs its actual live control owner");
    application_control_record *record = &app->controls[actor.slot];
    application_provider *source = source_provider(app, actor);
    if (!record->active || !qa_actor_id_equal(record->player.actor, actor) || !source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Body request lost its selected movement owner");
    if (!body_base_owner(source, record->player.state.kind)) return true;
    control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (!qa_actor_id_equal(input->actor, actor)) {
        if (!outputs->has_body_bounds) return true;
        (void)input_enroll(app->control_frames, actor.slot);
        if (input->actor.registry || input->turn) *input = (control_input){0};
        input->actor = actor; input->provider = source->owner;
        input->retained = source_client(source) && source->component.clock.kind == QA_RULESET_NETQUAKE &&
            record->player.state.kind == QA_RULESET_NETQUAKE;
    }
    if (input->provider != source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Body continuation changed its genuine source owner");
    if (outputs->has_body_bounds) {
        if (!input->has_body_base) { input->body_base = current; input->has_body_base = true; }
    } else if (input->has_body_base) {
        outputs->has_body_bounds = true; outputs->body_bounds = input->body_base;
        if (bounds_equal(current, input->body_base)) input->has_body_base = false;
    }
    if (!input->has_body_base && !input->seen && !input->retained) {
        input->actor = (qa_actor_id){0}; input->provider = 0;
    }
    return true;
}

void application_control_body_reset(qa_application *app, qa_actor_id actor)
{
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity) return;
    control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->actor, actor)) {
        input->has_body_base = false;
        if (!input->seen && !input->retained && !input->turn) {
            input->actor = (qa_actor_id){0}; input->provider = 0;
        }
    }
}

static bool execute_original_q3_group(qa_application *, control_group *, qa_error *);

static bool before_source(const application_provider *provider)
{
    application_provider *map = provider ? application_world_provider(provider->application, QA_ROLE_ENTITIES, "") : NULL;
    return map && map->kind != APPLICATION_PROVIDER_Q1 && map->kind != APPLICATION_PROVIDER_QC;
}

static uint64_t source_interval(qa_ruleset_id kind, uint64_t host_elapsed_ns)
{
    if (kind != QA_RULESET_NETQUAKE) return host_elapsed_ns;
    return host_elapsed_ns < UINT64_C(1000000) ? UINT64_C(1000000) :
        host_elapsed_ns > UINT64_C(100000000) ? UINT64_C(100000000) : host_elapsed_ns;
}

static bool weapon_owner(qa_application *app, qa_actor_id actor, qa_actor_owner owner, qa_item_id item)
{
    if (!owner && !item) return true;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    qa_item_definition definition;
    return owner && item && arsenal && arsenal->owner == owner &&
        qa_inventory_source_definition_read(app->inventory, actor, owner, item, &definition, NULL) &&
        definition.weapon && (definition.actions & QA_ITEM_USE) != 0;
}

bool application_control_frames_create(qa_application *app, qa_error *error)
{
    if (!app || app->control_frames || !app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Input owner needs a fresh control table");
    struct application_control_frames *frames = calloc(1, sizeof(*frames));
    if (!frames) return application_fail(error, QA_ERROR_MEMORY, "Allocating source input owner");
    frames->inputs = calloc(app->control_capacity, sizeof(*frames->inputs));
    if (!frames->inputs) { free(frames); return application_fail(error, QA_ERROR_MEMORY, "Allocating retained source inputs"); }
    frames->application = app; frames->capacity = app->control_capacity;
    app->control_frames = frames;
    return true;
}

void application_control_frames_free(struct application_control_frames *frames)
{
    if (!frames) return;
    while (frames->head) {
        control_group *group = frames->head; frames->head = group->next; free(group);
    }
    for (control_input_slot *slot = frames->first_input; slot; slot = slot->next)
        (void)application_control_turn_abort(slot->value.turn, NULL);
    free(frames->touched); free(frames->inputs); free(frames);
}

bool application_control_frames_idle(const qa_application *app)
{
    const struct application_control_frames *frames = app ? app->control_frames : NULL;
    if (!frames) return true;
    if (frames->current || frames->draining || frames->state || frames->q3_group || frames->mod_inputs) return false;
    for (const control_input_slot *slot = frames->first_input; slot; slot = slot->next)
        if (slot->value.turn || slot->value.source_turn_actor.registry) return false;
    return true;
}

bool application_control_frames_abort(qa_application *app, qa_error *error)
{
    if (!app || !app->control_frames) return true;
    struct application_control_frames *owner = app->control_frames;
    if (owner->current || owner->draining || owner->state)
        return application_fail(error, QA_ERROR_ARGUMENT, "Input cleanup cannot interrupt a current source call");
    bool ok = true; qa_error first = {0};
    for (control_input_slot *slot = owner->first_input; slot; slot = slot->next) {
        control_input *input = &slot->value;
        struct application_control_turn *turn = input->turn;
        input->turn = NULL; qa_error current = {0};
        input->source_turn_actor = (qa_actor_id){0};
        input->source_turn_provider = 0;
        input->source_turn_frame_number = 0;
        if (!application_control_turn_abort(turn, &current)) { if (ok) first = current; ok = false; }
    }
    owner->touched_count = 0;
    qa_error components = {0};
    if (!application_control_mod_abort_all(app, &components)) { if (ok) first = components; ok = false; }
    inputs_prune(owner);
    if (!ok && error) *error = first;
    return ok;
}

struct application_control_mod_input *application_control_mod_head(const qa_application *app)
{ return app && app->control_frames ? app->control_frames->mod_inputs : NULL; }
void application_control_mod_head_set(qa_application *app, struct application_control_mod_input *head)
{ app->control_frames->mod_inputs = head; }

bool application_control_group_touch_once(qa_application *app, qa_actor_id actor, qa_actor_id touched,
                                           bool *first, qa_error *error)
{
    *first = true;
    const application_control_context *context = application_control_frame_current(app, actor);
    if (!context || context->path != APPLICATION_CONTROL_QW_GROUP || !touched.registry) return true;
    struct application_control_frames *owner = app->control_frames;
    for (size_t i = 0; i < owner->touched_count; ++i)
        if (qa_actor_id_equal(owner->touched[i], touched)) { *first = false; return true; }
    if (owner->touched_count == owner->touched_capacity) {
        size_t capacity = owner->touched_capacity ? owner->touched_capacity * 2 : 16;
        if (capacity < owner->touched_capacity || capacity > SIZE_MAX / sizeof(*owner->touched))
            return application_fail(error, QA_ERROR_MEMORY, "QW group contact list is too large");
        qa_actor_id *items = realloc(owner->touched, capacity * sizeof(*items));
        if (!items) return application_fail(error, QA_ERROR_MEMORY, "Allocating QW group contact list");
        owner->touched = items; owner->touched_capacity = capacity;
    }
    owner->touched[owner->touched_count++] = touched;
    return true;
}

void application_control_frames_release(qa_application *app, qa_actor_id actor)
{
    struct application_control_frames *frames = app ? app->control_frames : NULL;
    if (!frames || actor.slot >= frames->capacity) return;
    application_control_frames_state(app, actor, NULL);
    control_input *input = &frames->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->source_turn_actor, actor)) {
        input->source_turn_actor = (qa_actor_id){0}; input->source_turn_provider = 0;
        input->source_turn_frame_number = 0;
    }
    if (qa_actor_id_equal(input->actor, actor) && !input->turn) *input = (control_input){0};
    control_group **link = &frames->head;
    frames->tail = NULL;
    while (*link) {
        control_group *group = *link;
        if (qa_actor_id_equal(group->actor, actor)) { *link = group->next; free(group); }
        else { frames->tail = group; link = &group->next; }
    }
}

bool application_control_frames_sequence(const qa_application *app, qa_actor_id actor, bool *seen, uint64_t *sequence)
{
    if (!app || !seen || !sequence || actor.slot >= app->control_capacity) return false;
    const application_control_record *record = &app->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->player.actor, actor)) return false;
    *seen = record->command_seen; *sequence = record->player.command_sequence;
    const struct application_control_frames *frames = app->control_frames;
    if (frames && qa_actor_id_equal(frames->inputs[actor.slot].value.actor, actor) && frames->inputs[actor.slot].value.seen) {
        *seen = true; *sequence = frames->inputs[actor.slot].value.sequence;
    }
    return true;
}

bool application_control_frames_q1_prepared(const qa_application *app, qa_actor_id actor,
                                             qa_actor_owner provider)
{
    qa_source_frame actual;
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity ||
        !qa_session_active_frame(app->session, provider, &actual)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot].value;
    return qa_actor_id_equal(input->q1_source_actor, actor) && input->q1_source_provider == provider &&
        input->q1_source_frame_number == actual.number && !input->q1_source_deferred;
}

bool application_control_frames_q1_command_ready(const qa_application *app, qa_actor_id actor,
    qa_actor_owner provider, const qa_movement_command *command)
{
    qa_source_frame actual;
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity ||
        !qa_session_active_frame(app->session, provider, &actual)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (!input->q1_source_deferred || !qa_actor_id_equal(input->q1_source_actor, actor) ||
        input->q1_source_provider != provider || input->q1_source_frame_number != actual.number) return true;
    return command && command->sequence == input->q1_source_sequence;
}

bool application_control_frames_q1_complete(qa_application *app, qa_actor_id actor,
                                             qa_actor_owner provider, qa_error *error)
{
    qa_source_frame actual;
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity ||
        !qa_session_active_frame(app->session, provider, &actual) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source preparation lost its real frame");
    control_input *input = &app->control_frames->inputs[actor.slot].value;
    (void)input_enroll(app->control_frames, actor.slot);
    input->q1_source_actor = actor; input->q1_source_provider = provider;
    input->q1_source_frame_number = actual.number;
    input->q1_source_deferred = false; input->q1_source_sequence = 0;
    return true;
}

static void consume_unified_impulse(control_unified *receipt)
{
    receipt->impulse = 0;
    switch (receipt->movement.kind) {
    case QA_RULESET_NETQUAKE: receipt->movement.data.nq.impulse = 0; break;
    case QA_RULESET_QUAKEWORLD: receipt->movement.data.qw.impulse = 0; break;
    case QA_RULESET_Q2_CLASSIC: receipt->movement.data.q2.impulse = 0; break;
    case QA_RULESET_Q2_RERELEASE: break;
    case QA_RULESET_Q3: break;
    }
}

void application_control_frames_consume_impulse(qa_application *app, qa_actor_id actor,
    uint64_t sequence)
{
    struct application_control_frames *owner = app ? app->control_frames : NULL;
    if (!owner || actor.slot >= owner->capacity) return;
    control_input *input = &owner->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->actor, actor) && input->seen && input->sequence == sequence) {
        input->latest.impulse = input->impulse = 0;
        if (input->domain == CONTROL_COMMAND_UNIFIED) consume_unified_impulse(&input->unified);
    }
    for (control_group *group = owner->head; group; group = group->next) {
        if (!qa_actor_id_equal(group->actor, actor)) continue;
        for (size_t i = 0; i < group->count; ++i)
            if (group->commands[i].sequence == sequence) group->commands[i].impulse = 0;
        if (group->domain == CONTROL_COMMAND_UNIFIED && group->unified.sequence == sequence)
            consume_unified_impulse(&group->unified);
    }
    const qa_movement_call *call = application_control_frames_call_current(app, actor);
    if (call && call->command && call->command->sequence == sequence) call->command->impulse = 0;
}

static bool qc_receipt(qa_application *app, qa_actor_id actor, uint64_t ordinal,
    qa_movement_command *command, qa_error *error)
{
    static const qa_launch_role roles[] = {QA_ROLE_ENTITIES, QA_ROLE_MOVEMENT, QA_ROLE_CHARACTER,
        QA_ROLE_ARSENAL, QA_ROLE_INVENTORY, QA_ROLE_COMBAT, QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT};
    application_provider *owners[sizeof(roles) / sizeof(roles[0])] = {0};
    const qa_movement_command raw = *command;
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
        application_provider *owner = roles[i] == QA_ROLE_ENTITIES
            ? application_world_provider(app, roles[i], "") : application_provider_for(app, actor, roles[i], "");
        owners[i] = owner;
        if (!owner || owner->kind != APPLICATION_PROVIDER_QC || !owner->constructed ||
            !owner->attached || owner->close_pending || !owner->state.qc.engine ||
            owner->state.qc.engine->profile != QA_QC_RERELEASE) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) duplicate |= owners[j] == owner;
        if (duplicate) continue;
        bool member = false;
        if (!application_qc_control_source_client(owner, actor, &member, error)) return false;
        if (!member) continue;
        qa_movement_command received = raw;
        if (!application_qc_player_receive(owner, actor, ordinal, &received, error)) return false;
        if (received.forward_move == 0 && raw.forward_move != 0) command->forward_move = 0;
        if (received.side_move == 0 && raw.side_move != 0) command->side_move = 0;
    }
    return true;
}

static bool receive_q3_command(qa_application *app, qa_actor_id actor,
    uint64_t sequence, uint64_t ordinal, const qa_q3_usercmd *raw, qa_error *error)
{
    if (!app || !raw || !app->control_frames || app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q3 input needs a running admitted source client");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    uint32_t slot = 0; bool deferred = false;
    qa_clock_state clock;
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->player.actor, actor) ||
        !provider || (provider->kind != APPLICATION_PROVIDER_Q3 && !original_q3(provider)) || !provider->constructed ||
        !provider->attached || provider->close_pending || frames->draining || frames->current ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") || !provider->component.command_actor ||
        !provider->component.command_actor(provider->component.state, app->session, actor) ||
        !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q3 input has no actual source admission");
    if (provider->kind == APPLICATION_PROVIDER_Q3 &&
        (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
         !application_native_q3_client_deferred(provider, actor, &deferred, error))) return false;
    bool seen; uint64_t previous;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    if (seen && sequence <= previous)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q3 input sequence was already admitted");
    control_input *input = &frames->inputs[actor.slot].value;
    if (input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q3 intake cannot interrupt a retained input turn");
    control_group *group = deferred ? NULL : malloc(sizeof(*group) + sizeof(qa_movement_command));
    if (!deferred && !group)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating raw Q3 source command");
    qa_movement_command command = q3_command(raw, sequence);
    application_snapshot_mutated(app);
    if (!qc_receipt(app, actor, ordinal, &command, error)) {
        free(group); application_fault(app, error); return false;
    }
    qa_q3_usercmd received = q3_source_command(&command, true);
    if (group) {
        *group = (control_group){.actor = actor, .provider = provider->owner, .count = 1,
            .before_source = before_source(provider), .domain = CONTROL_COMMAND_Q3_SOURCE,
            .source_time_ns = clock.frame.time_ns};
        group->commands[0] = command;
    }
    if (provider->kind == APPLICATION_PROVIDER_Q3 &&
        (!application_native_q3_wire_command(provider, slot, &received, error) ||
         !qa_q3_client_received_command(provider->state.q3, actor, &received, error))) {
        free(group); application_fault(app, error); return false;
    }
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true;
    input->retained = false; input->domain = CONTROL_COMMAND_Q3_SOURCE;
    input->accepted_time_ns = input->source_time_ns = clock.frame.time_ns; input->qw_receipt_time_ns = 0; input->latest = command;
    input->arsenal = 0; input->weapon = 0; input->impulse = 0;
    if (group && original_q3(provider)) {
        bool ok = execute_original_q3_group(app, group, error);
        free(group);
        if (!ok) application_fault(app, error);
        return ok;
    }
    if (group) {
        if (frames->tail) frames->tail->next = group; else frames->head = group;
        frames->tail = group;
    }
    return true;
}

bool qa_application_control_q3_command(qa_application *app, qa_actor_id actor,
    uint64_t sequence, const qa_q3_usercmd *raw, qa_error *error)
{
    return receive_q3_command(app, actor, sequence, 0, raw, error);
}

bool qa_application_control_q2_command(qa_application *app, qa_actor_id actor,
    uint64_t sequence, const qa_movement_command *raw, qa_error *error)
{
    if (!app || !raw || !app->control_frames || app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q2 input needs an admitted source client");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    qa_clock_state clock;
    qa_movement_result source;
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->player.actor, actor) ||
        !provider || (provider->kind != APPLICATION_PROVIDER_Q2 &&
            !application_native_q2_source_client(provider, actor) &&
            !application_native_q2_declared_source_client(provider, actor)) || !provider->constructed ||
        !provider->attached || provider->close_pending || frames->draining || frames->current ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !provider->component.command_actor ||
        !provider->component.command_actor(provider->component.state, app->session, actor) ||
        !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q2 input lacks its physical source and literal dialect");
    if (application_native_q2_declared_source_client(provider, actor)) {
        if (!application_native_q2_declared_raw_capable(provider))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Declared native Q2 has no admitted raw Source command phase");
        if (application_provider_for(app, actor, QA_ROLE_ARSENAL, NULL) != provider)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Declared native Q2 has no isolated foreign arsenal phase");
    }
    bool physical = provider->kind == APPLICATION_PROVIDER_Q2
        ? qa_q2_player_movement_read(provider->state.q2, actor, &source, NULL, error)
        : application_native_q2_source_client(provider, actor)
            ? application_native_q2_input_read(provider, actor, &source, NULL, error)
            : application_native_q2_declared_input_read(provider, actor, &source.state, error);
    if (!physical) return false;
    if (raw->kind != source.state.kind || !q2_raw_valid(raw))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q2 input differs from its physical Source command fields");
    bool seen; uint64_t previous;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    control_input *input = &frames->inputs[actor.slot].value;
    if ((seen && sequence <= previous) || input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q2 input sequence or retained turn is invalid");
    control_group *group = malloc(sizeof(*group) + sizeof(qa_movement_command));
    if (!group) return application_fail(error, QA_ERROR_MEMORY, "Allocating physical Q2 input");
    qa_movement_command command = *raw; command.sequence = sequence;
    if (!qc_receipt(app, actor, 0, &command, error)) {
        free(group); application_fault(app, error); return false;
    }
    *group = (control_group){.actor = actor, .provider = provider->owner, .count = 1,
        .before_source = before_source(provider), .domain = CONTROL_COMMAND_Q2_SOURCE,
        .source_time_ns = clock.frame.time_ns};
    group->commands[0] = command;
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true;
    input->retained = false; input->domain = CONTROL_COMMAND_Q2_SOURCE;
    input->accepted_time_ns = input->source_time_ns = clock.frame.time_ns; input->qw_receipt_time_ns = 0;
    input->latest = command; input->arsenal = 0; input->weapon = 0; input->impulse = 0;
    if (frames->tail) frames->tail->next = group; else frames->head = group;
    frames->tail = group;
    return true;
}

bool qa_application_control_nq_command(qa_application *app, qa_actor_id actor,
    uint64_t sequence, const qa_q1_command *raw, qa_error *error)
{
    if (!app || !raw || !app->control_frames || app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw NQ input needs a running admitted source client");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    qa_clock_state clock;
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->player.actor, actor) ||
        !source_client(provider) || provider->component.clock.kind != QA_RULESET_NETQUAKE ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !provider->component.command_actor ||
        !provider->component.command_actor(provider->component.state, app->session, actor) ||
        frames->draining || frames->current || !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw NQ input has no actual physical source client");
    if (!nq_selected_sources_ready(app, actor))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Retained NQ input needs the selected external source's staged movement and weapon bridge");
    control_input *input = &frames->inputs[actor.slot].value;
    bool seen; uint64_t previous;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    qa_movement_command command = {.kind = QA_RULESET_NETQUAKE, .sequence = sequence,
        .acknowledged_server_seconds = raw->time,
        .angles = {raw->angles[0], raw->angles[1], raw->angles[2]},
        .forward_move = raw->forward, .side_move = raw->side, .up_move = raw->up,
        .buttons = raw->buttons, .impulse = raw->impulse};
    if (!nq_raw_valid(&command) || (seen && sequence <= previous) || input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw NQ input has invalid values, sequence or an open turn");
    for (const control_group *group = frames->head; group; group = group->next)
        if (qa_actor_id_equal(group->actor, actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Raw NQ input cannot replace queued selected input");
    if (app->q1_paused) return true;
    if (!qc_receipt(app, actor, 0, &command, error)) { application_fault(app, error); return false; }
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true;
    input->retained = true; input->domain = CONTROL_COMMAND_NQ_SOURCE;
    input->accepted_time_ns = input->source_time_ns = clock.frame.time_ns; input->qw_receipt_time_ns = 0;
    input->arsenal = 0; input->weapon = 0;
    if (command.impulse) input->impulse = command.impulse;
    command.impulse = 0; input->latest = command;
    return true;
}

bool qa_application_control_qw_commands(qa_application *app, qa_actor_id actor,
    const qa_movement_command *commands, size_t count, qa_error *error)
{
    if (!app || !commands || !count || count > 20 || !app->control_frames ||
        app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW group needs a running admitted source client");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    qa_clock_state clock;
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->player.actor, actor) ||
        !source_client(provider) || provider->component.clock.kind != QA_RULESET_QUAKEWORLD ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !provider->component.command_actor ||
        !provider->component.command_actor(provider->component.state, app->session, actor) ||
        frames->draining || frames->current || !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW group has no actual physical source client");
    bool seen; uint64_t previous;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    for (size_t i = 0; i < count; ++i)
        if (!command_valid(&commands[i]) || commands[i].kind != QA_RULESET_QUAKEWORLD ||
            commands[i].milliseconds > 255 || commands[i].sequence != commands[0].sequence ||
            (seen && commands[i].sequence <= previous))
            return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW group has invalid source commands or packet sequence");
    if (app->q1_paused) return true;
    uint64_t receipt_time;
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        if (!application_qc_control_receipt_time(provider, actor, &receipt_time, error)) return false;
    } else {
        double source_elapsed;
        if (!qa_q1_game_clock_read(provider->state.q1, &receipt_time, &source_elapsed))
            return application_fail(error, QA_ERROR_ARGUMENT, "QW source receipt lost its native gameplay clock");
    }
    if (clock.frame.time_ns > UINT64_MAX - clock.debt_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW current source clock exhausted");
    uint64_t source_time = clock.frame.time_ns + clock.debt_ns;
    if (receipt_time > source_time)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source receipt clock exceeds its real admission");
    control_input *input = &frames->inputs[actor.slot].value;
    if (input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW intake cannot interrupt a retained turn");
    control_group *group = malloc(sizeof(*group) + count * sizeof(*commands));
    if (!group) return application_fail(error, QA_ERROR_MEMORY, "Allocating raw QW source group");
    *group = (control_group){.actor = actor, .provider = provider->owner, .count = count,
        .quakeworld = true, .before_source = before_source(provider), .domain = CONTROL_COMMAND_QW_SOURCE,
        .source_time_ns = source_time};
    for (size_t i = 0; i < count; ++i) {
        group->commands[i] = commands[i];
        if (!qc_receipt(app, actor, (uint64_t)i, &group->commands[i], error)) {
            free(group); application_fault(app, error); return false;
        }
    }
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = commands[0].sequence; input->seen = true;
    input->retained = false; input->domain = CONTROL_COMMAND_QW_SOURCE; input->accepted_time_ns = input->source_time_ns = source_time;
    input->qw_receipt_time_ns = receipt_time;
    input->latest = group->commands[count - 1]; input->arsenal = 0; input->weapon = 0; input->impulse = 0;
    if (frames->tail) frames->tail->next = group; else frames->head = group;
    frames->tail = group;
    return true;
}

bool application_control_last_qw_command(const qa_application *app, qa_actor_id actor,
    qa_movement_command *out, uint64_t *time_ns, bool *present, qa_error *error)
{
    if (!app || !out || !time_ns || !present || !app->control_frames || actor.slot >= app->control_capacity ||
        !app->controls[actor.slot].active || !qa_actor_id_equal(app->controls[actor.slot].player.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source input read needs a current full control actor");
    *out = (qa_movement_command){0}; *time_ns = 0; *present = false;
    const control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (!qa_actor_id_equal(input->actor, actor) || !input->seen || input->domain != CONTROL_COMMAND_QW_SOURCE ||
        input->latest.kind != QA_RULESET_QUAKEWORLD)
        return true;
    *out = input->latest; *time_ns = input->qw_receipt_time_ns; *present = true;
    return true;
}

bool qa_application_control_unified_command(qa_application *app, qa_actor_id actor,
    const qa_unified_input *raw, qa_error *error)
{
    if (!app || !raw || !app->control_frames || app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input needs its admitted live control");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *source = source_provider(app, actor);
    control_unified receipt = {.movement = raw->command, .sequence = raw->sequence,
        .has_arsenal = raw->has_arsenal};
    if (raw->has_arsenal) {
        application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
        receipt.arsenal = qa_strings_find(qa_session_strings(app->session), raw->arsenal.provider);
        receipt.weapon = raw->arsenal.weapon.size
            ? qa_strings_find(qa_session_strings(app->session), raw->arsenal.weapon) : 0;
        receipt.use_holdable = raw->arsenal.use_holdable;
        receipt.has_impulse = raw->arsenal.has_impulse; receipt.impulse = raw->arsenal.impulse;
        if (!arsenal || receipt.arsenal != arsenal->owner ||
            (raw->arsenal.weapon.size && !receipt.weapon))
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified arsenal intent is not declared by its selected owner");
        if (receipt.weapon) {
            if (!weapon_owner(app, actor, receipt.arsenal, receipt.weapon))
                return application_fail(error, QA_ERROR_ARGUMENT, "Unified weapon has no usable declaration from its selected arsenal");
        }
    }
    bool seen = false; uint64_t previous = 0;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->player.actor, actor) ||
        !source || !source->constructed || !source->attached || source->close_pending ||
        frames->current || frames->draining || !unified_valid(&receipt) ||
        receipt.movement.kind != record->player.state.kind || (seen && receipt.sequence <= previous))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input lost its selected actor, dialect or sequence");
    control_input *input = &frames->inputs[actor.slot].value;
    if (input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input cannot interrupt a prepared Source turn");
    if (app->q1_paused) return true;
    qa_clock_state accepted_clock;
    if (!qa_session_clock(app->session, source->owner, &accepted_clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified command has no registered Source clock");
    bool retained = source_client(source) && source->component.clock.kind == QA_RULESET_NETQUAKE &&
        receipt.movement.kind == QA_RULESET_NETQUAKE;
    control_group *group = retained ? NULL : calloc(1, sizeof(*group) + sizeof(*group->commands));
    if (!retained && !group) return application_fail(error, QA_ERROR_MEMORY, "Allocating unified command receipt");
    qa_movement_command marker = {.kind = receipt.movement.kind, .sequence = receipt.sequence};
    if (group) {
        *group = (control_group){.actor = actor, .provider = source->owner, .count = 1,
            .before_source = before_source(source), .domain = CONTROL_COMMAND_UNIFIED, .unified = receipt};
        group->commands[0] = marker;
    }
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = source->owner; input->sequence = receipt.sequence; input->seen = true;
    input->retained = retained; input->domain = CONTROL_COMMAND_UNIFIED;
    input->source_time_ns = 0; input->accepted_time_ns = accepted_clock.frame.time_ns; input->qw_receipt_time_ns = 0;
    input->latest = marker; input->unified = receipt;
    input->arsenal = 0; input->weapon = 0; input->impulse = 0;
    if (group) {
        if (frames->tail) frames->tail->next = group; else frames->head = group;
        frames->tail = group;
    }
    return true;
}

bool application_control_frames_receive(qa_application *app, qa_actor_id actor,
                                         const qa_movement_command *commands, size_t count, qa_error *error)
{
    if (!app || !commands || count == 0 || !app->control_frames ||
        app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        actor.slot >= app->control_capacity || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Input group needs a running admitted actor");
    struct application_control_frames *frames = app->control_frames;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *provider = source_provider(app, actor);
    if (!record->active || record->retired || !qa_actor_id_equal(record->player.actor, actor) || !provider)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Input group has no current execution owner");
    if ((provider->kind == APPLICATION_PROVIDER_Q3 || original_q3(provider)) &&
        record->player.state.kind == QA_RULESET_Q3) {
        bool seen; uint64_t previous;
        (void)application_control_frames_sequence(app, actor, &seen, &previous);
        for (size_t i = 0; i < count; ++i)
            if (!command_valid(&commands[i]) || commands[i].kind != QA_RULESET_Q3 ||
                (seen && commands[i].sequence <= previous) ||
                (i && commands[i].sequence <= commands[i - 1].sequence))
                return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 input group has invalid commands");
        for (size_t i = 0; i < count; ++i) {
            qa_q3_usercmd raw = q3_source_command(&commands[i], false);
            if (!receive_q3_command(app, actor, commands[i].sequence, (uint64_t)i, &raw, error)) return false;
        }
        return true;
    }
    if (record->player.state.kind == QA_RULESET_QUAKEWORLD && source_client(provider) &&
        provider->component.clock.kind == QA_RULESET_QUAKEWORLD && provider->component.command_actor &&
        provider->component.command_actor(provider->component.state, app->session, actor))
        return qa_application_control_qw_commands(app, actor, commands, count, error);
    bool q2_source = (record->player.state.kind == QA_RULESET_Q2_CLASSIC &&
        provider->component.clock.kind == QA_RULESET_Q2_CLASSIC) ||
        (record->player.state.kind == QA_RULESET_Q2_RERELEASE &&
         provider->component.clock.kind == QA_RULESET_Q2_RERELEASE);
    if (q2_source && provider == application_world_provider(app, QA_ROLE_ENTITIES, "") &&
        (provider->kind == APPLICATION_PROVIDER_Q2 || native_q2_raw_source_client(provider, actor))) {
        for (size_t i = 0; i < count; ++i) {
            qa_movement_command raw = commands[i];
            raw.weapon = 0; raw.server_time_ms = 0; raw.acknowledged_server_seconds = 0;
            if (raw.kind == QA_RULESET_Q2_CLASSIC) {
                raw.server_frame = 0;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    uint16_t word = (uint16_t)raw.angle_words[axis];
                    raw.angle_words[axis] = word <= INT16_MAX ? word : (int32_t)word - 65536;
                }
            }
            if (!qa_application_control_q2_command(app, actor, raw.sequence, &raw, error)) return false;
        }
        return true;
    }
    if (frames->draining || (frames->current && qa_actor_id_equal(frames->current->actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Input intake cannot replace an applied command");
    bool seen; uint64_t sequence;
    (void)application_control_frames_sequence(app, actor, &seen, &sequence);
    bool retained = source_client(provider) && provider->component.clock.kind == QA_RULESET_NETQUAKE &&
        record->player.state.kind == QA_RULESET_NETQUAKE;
    bool quakeworld = source_client(provider) && provider->component.clock.kind == QA_RULESET_QUAKEWORLD &&
        record->player.state.kind == QA_RULESET_QUAKEWORLD;
    if (quakeworld && count > 20)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW input group exceeds its twenty-command source bound");
    for (size_t i = 0; i < count; ++i) {
        if (!command_valid(&commands[i]) || commands[i].kind != record->player.state.kind ||
            (commands[i].kind == QA_RULESET_QUAKEWORLD && commands[i].milliseconds > 255) ||
            (i && quakeworld ? commands[i].sequence != sequence : seen && commands[i].sequence <= sequence))
            return application_fail(error, QA_ERROR_ARGUMENT, "Input group has invalid dialect, values or sequence");
        seen = true; sequence = commands[i].sequence;
    }
    if (app->q1_paused) return true;
    qa_clock_state accepted_clock;
    if (!qa_session_clock(app->session, provider->owner, &accepted_clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected command has no registered Source clock");
    control_group *group = NULL;
    if (!retained) {
        if (count > (SIZE_MAX - sizeof(*group)) / sizeof(*commands))
            return application_fail(error, QA_ERROR_MEMORY, "Input group is too large");
        group = malloc(sizeof(*group) + count * sizeof(*commands));
        if (!group) return application_fail(error, QA_ERROR_MEMORY, "Allocating pending input group");
        *group = (control_group){.actor = actor, .provider = provider->owner, .count = count,
            .quakeworld = quakeworld, .before_source = before_source(provider)};
    }
    control_input *input = &frames->inputs[actor.slot].value;
    if (input->turn) { free(group); return application_fail(error, QA_ERROR_ARGUMENT, "Retained input preparation is open"); }
    qa_movement_command latest = {0};
    for (size_t i = 0; i < count; ++i) {
        latest = commands[i];
        if (!qc_receipt(app, actor, (uint64_t)i, &latest, error)) {
            free(group); application_fault(app, error); return false;
        }
        if (group) group->commands[i] = latest;
    }
    (void)input_enroll(frames, actor.slot);
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true; input->retained = retained;
    input->domain = CONTROL_COMMAND_SELECTED; input->source_time_ns = 0; input->accepted_time_ns = accepted_clock.frame.time_ns; input->qw_receipt_time_ns = 0;
    input->arsenal = 0; input->weapon = 0;
    input->latest = latest;
    if (retained) {
        for (size_t i = 0; i < count; ++i) if (commands[i].impulse) input->impulse = commands[i].impulse;
        input->latest.impulse = 0;
    } else {
        if (frames->tail) frames->tail->next = group; else frames->head = group;
        frames->tail = group;
    }
    return true;
}

bool application_control_frames_receive_bot(qa_application *app, qa_actor_id actor,
                                             const qa_movement_command *command,
                                             qa_actor_owner arsenal, qa_item_id weapon, qa_error *error)
{
    if (!app || !application_player_bot(app, actor) || !application_bots_actor(app, actor) ||
        !weapon_owner(app, actor, arsenal, weapon))
        return application_fail(error, QA_ERROR_ARGUMENT, "Bot input needs its actual producer and selected item owner");
    if (!application_control_frames_receive(app, actor, command, 1, error)) return false;
    if (app->q1_paused) return true;
    control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (input->retained || input->domain == CONTROL_COMMAND_Q3_SOURCE) {
        input->arsenal = arsenal; input->weapon = weapon;
        if (app->control_frames->tail && qa_actor_id_equal(app->control_frames->tail->actor, actor) &&
            app->control_frames->tail->commands[0].sequence == command->sequence) {
            app->control_frames->tail->arsenal = arsenal;
            app->control_frames->tail->weapon = weapon; app->control_frames->tail->bot = true;
        }
    }
    else {
        app->control_frames->tail->arsenal = arsenal; app->control_frames->tail->weapon = weapon;
        app->control_frames->tail->bot = true;
    }
    return true;
}

bool application_control_last_mod_command(const qa_application *app, qa_actor_id actor,
    const qa_q3_player *player, qa_q3_usercmd *out, qa_error *error)
{
    if (!app || !player || !out || !app->control_frames || actor.slot >= app->control_capacity ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        !app->controls[actor.slot].active || app->controls[actor.slot].retired ||
        !qa_actor_id_equal(app->controls[actor.slot].player.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component command needs its admitted actual actor");
    const control_input *input = &app->control_frames->inputs[actor.slot].value;
    application_provider *source = source_provider((qa_application *)app, actor);
    if (!qa_actor_id_equal(input->actor, actor) || !input->seen || !source ||
        !source->constructed || !source->attached || source->close_pending || source->owner != input->provider)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Component client has no accepted destination command");
    if (input->domain == CONTROL_COMMAND_UNIFIED)
        return unified_mod_command(&input->unified, player, input->accepted_time_ns, out, error);
    const qa_movement_command *command = &input->latest;
    if (!command_valid(command))
        return application_fail(error, QA_ERROR_FORMAT, "Accepted component command is invalid");
    qa_q3_usercmd result = {.serverTime = command->kind == QA_RULESET_Q3 ? command->server_time_ms :
        command_word((uint32_t)(input->accepted_time_ns / UINT64_C(1000000))),
        .buttons = command_word(command->kind == QA_RULESET_Q3 ? command->buttons : command->buttons & 1u),
        .weapon = (uint8_t)player->weapon};
    if (command->kind == QA_RULESET_Q3) {
        const float axes[] = {command->forward_move, command->side_move, command->up_move};
        for (unsigned i = 0; i < 3; ++i)
            if (axes[i] < -128 || axes[i] > 127 || axes[i] != truncf(axes[i]))
                return application_fail(error, QA_ERROR_FORMAT, "Accepted Q3 component command exceeds its byte axes");
        result = q3_source_command(command, true);
        result.weapon = (uint8_t)player->weapon;
    } else {
        qa_movement_command source_command = *command, converted;
        qa_input_command_basis from = {.kind = command->kind, .words = command->kind == QA_RULESET_Q2_CLASSIC};
        qa_input_command_basis to = {.kind = QA_RULESET_Q3, .words = true,
            .relative = input->domain == CONTROL_COMMAND_SELECTED};
        memcpy(to.delta_words, player->deltaAngles, sizeof(to.delta_words));
        if (command->kind == QA_RULESET_Q2_RERELEASE)
            source_command.up_move = command->buttons & 8u ? 200 : command->buttons & 16u ? -200 : 0;
        else if ((command->kind == QA_RULESET_NETQUAKE || command->kind == QA_RULESET_QUAKEWORLD) &&
                 (command->buttons & 2u)) source_command.up_move = 320;
        qa_input_command_convert(&source_command, NULL, &from, &to,
            (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_TRUNCATE,
                .clamp = true, .minimum = -127, .maximum = 127}, &converted);
        result.forwardmove = (int8_t)converted.forward_move;
        result.rightmove = (int8_t)converted.side_move; result.upmove = (int8_t)converted.up_move;
        memcpy(result.angles, converted.angle_words, sizeof(result.angles));
    }
    *out = result; return true;
}

bool application_control_frames_apply_nested(qa_application *app, qa_actor_id actor,
                                              const qa_movement_command *command,
                                              qa_movement_command *applied, qa_error *error)
{
    if (!app || !app->control_frames || !application_guest_input_applying(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Nested locomotion requires an actual guest input call");
    struct application_control_frames *owner = app->control_frames;
    const application_control_context *outer = application_control_frame_current(app, actor);
    uint64_t source_elapsed;
    if (!application_guest_input_interval(app, actor, &source_elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT, "Nested movement lost its genuine original source slice");
    application_control_context current = {.actor = actor, .path = APPLICATION_CONTROL_MIXED,
        .stage = APPLICATION_CONTROL_COMMAND, .source_guestcmd = true, .source_input_applied = true};
    if (outer) current = *outer;
    else {
        application_provider *source = source_provider(app, actor);
        if (!original_q3(source))
            return application_fail(error, QA_ERROR_ARGUMENT, "Nested original movement lost its physical source owner");
        if (qa_session_active_command(app->session, source->owner, &current.command) &&
            qa_actor_id_equal(current.command.actor, actor)) current.command_only = true;
        else if (!qa_session_active_frame(app->session, source->owner, &current.frame))
            return application_fail(error, QA_ERROR_ARGUMENT, "Nested original movement has no actual source admission");
    }
    current.source_guestcmd = true; current.source_input_applied = true;
    current.source_elapsed_ns = source_interval(command->kind, source_elapsed);
    const application_control_context *previous = owner->current; owner->current = &current;
    bool ok = application_control_move_applied(app, actor, command, applied, error);
    owner->current = previous;
    return ok;
}

const application_control_context *application_control_frame_current(const qa_application *app, qa_actor_id actor)
{
    const struct application_control_frames *frames = app ? app->control_frames : NULL;
    if (!frames || !frames->current || !qa_actor_id_equal(frames->current->actor, actor)) return NULL;
    const application_control_context *current = frames->current;
    qa_actor_owner owner = application_control_provider(current);
    if (current->command_only) {
        qa_source_command actual;
        if (!qa_session_active_command(app->session, owner, &actual) || !qa_actor_id_equal(actual.actor, actor) ||
            actual.kind != current->command.kind || actual.phase != current->command.phase ||
            actual.completed_frame_number != current->command.completed_frame_number ||
            actual.time_ns != current->command.time_ns || actual.elapsed_ns != current->command.elapsed_ns ||
            actual.host_elapsed_ns != current->command.host_elapsed_ns) return NULL;
        if (current->source_usercmd || current->source_guestcmd || current->source_q2cmd) {
            application_provider *source = NULL;
            for (size_t i = 0; i < app->provider_count; ++i)
                if (app->providers[i]->owner == owner) { source = app->providers[i]; break; }
            uint32_t slot;
            if (!source || !source->attached || !source->constructed || source->close_pending ||
                (current->source_usercmd && (source->kind != APPLICATION_PROVIDER_Q3 ||
                 !qa_q3_native_client_slot(source->state.q3, actor, &slot, NULL))) ||
                (current->source_guestcmd && (!original_q3(source) || !source->component.command_actor ||
                 !source->component.command_actor(source->component.state, app->session, actor))) ||
                (current->source_q2cmd && (source->kind != APPLICATION_PROVIDER_Q2 ||
                 source != application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "") ||
                 !source->component.command_actor ||
                 !source->component.command_actor(source->component.state, app->session, actor)))) return NULL;
        }
    } else {
        application_provider *source = source_provider((qa_application *)app, actor);
        if (!source || source->owner != owner) return NULL;
        if (current->source_nqcmd && (current->command_only || current->path != APPLICATION_CONTROL_NQ_TURN ||
            !source_client(source) || source != application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "") ||
            source->component.clock.kind != QA_RULESET_NETQUAKE || !source->component.command_actor ||
            !source->component.command_actor(source->component.state, app->session, actor))) return NULL;
        qa_source_frame actual;
        if (!qa_session_active_frame(app->session, owner, &actual) || actual.kind != current->frame.kind ||
            actual.number != current->frame.number || actual.time_ns != current->frame.time_ns ||
            actual.start_ns != current->frame.start_ns || actual.elapsed_ns != current->frame.elapsed_ns) return NULL;
    }
    if (current->path == APPLICATION_CONTROL_MIXED) {
        uint64_t interval = current->source_usercmd || current->source_q2cmd || current->unified_command ? current->command.elapsed_ns :
            current->command_only ? current->command.host_elapsed_ns : current->frame.elapsed_ns;
        if (current->source_guestcmd && current->source_input_applied) {
            if (!application_guest_input_interval(app, actor, &interval)) return NULL;
        } else if (!current->source_usercmd && !current->source_q2cmd && !current->unified_command)
            (void)qa_session_advance_interval(app->session, &interval);
        if (actor.slot >= app->control_capacity || source_interval(app->controls[actor.slot].player.state.kind, interval)
            != current->source_elapsed_ns) return NULL;
    }
    return frames->current;
}

application_control_outcome application_control_frames_q3_move(qa_application *app, const qa_source_command *admission,
    const qa_movement_command *command, bool use_holdable, qa_error *error)
{
    if (!app || !app->control_frames || !admission || !command || admission->kind != QA_RULESET_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 movement needs its actual source command");
    struct application_control_frames *owner = app->control_frames;
    application_control_context current = {.actor = admission->actor, .path = APPLICATION_CONTROL_MIXED,
        .stage = APPLICATION_CONTROL_COMMAND, .retained = true, .command_only = true,
        .command = *admission, .source_elapsed_ns = source_interval(command->kind, admission->elapsed_ns),
        .source_usercmd = true, .source_holdable = use_holdable};
    const control_input *input = &owner->inputs[admission->actor.slot].value;
    if (owner->q3_group && qa_actor_id_equal(owner->q3_group->actor, admission->actor)) {
        current.arsenal = owner->q3_group->arsenal; current.weapon = owner->q3_group->weapon;
    } else if (qa_actor_id_equal(input->actor, admission->actor)) {
        current.arsenal = input->arsenal; current.weapon = input->weapon;
    }
    const application_control_context *previous = owner->current;
    owner->current = &current;
    qa_movement_command unified_selected;
    bool ok = true;
    if (owner->q3_group && owner->q3_group->domain == CONTROL_COMMAND_UNIFIED) {
        unified_context(&current, &owner->q3_group->unified);
        ok = unified_command(app, admission->actor, &owner->q3_group->unified, &unified_selected, error);
        if (ok) {
            unified_selected.milliseconds = command->milliseconds;
            if (unified_selected.kind == QA_RULESET_Q3) unified_selected.server_time_ms = command->server_time_ms;
            command = &unified_selected;
        }
    }
    application_control_outcome outcome = ok
        ? application_control_stage_move(app, admission->actor, command, NULL, error)
        : application_control_finish(app, admission->actor, false, "control/q3-command", error);
    owner->current = previous;
    ok = outcome != APPLICATION_CONTROL_FAILED;
    if (ok && qa_actor_id_equal(input->actor, admission->actor) &&
        (!owner->q3_group || input->sequence == owner->q3_group->commands[0].sequence)) {
        owner->inputs[admission->actor.slot].value.arsenal = 0;
        owner->inputs[admission->actor.slot].value.weapon = 0;
    }
    return outcome;
}

void application_control_frames_state(qa_application *app, qa_actor_id actor, qa_movement_state *state)
{
    struct application_control_frames *owner = app ? app->control_frames : NULL;
    if (!owner) return;
    if (!state) {
        if (qa_actor_id_equal(owner->state_actor, actor)) {
            owner->state = NULL; owner->state_actor = (qa_actor_id){0};
        }
    } else if (application_control_frame_current(app, actor)) {
        owner->state = state; owner->state_actor = actor;
    }
}

qa_movement_state *application_control_frames_state_current(const qa_application *app, qa_actor_id actor)
{
    const struct application_control_frames *owner = app ? app->control_frames : NULL;
    if (!owner || !application_control_frame_current(app, actor)) return NULL;
    if (owner->call && qa_actor_id_equal(owner->call->actor, actor)) return owner->call->state;
    return owner->state && qa_actor_id_equal(owner->state_actor, actor) ? owner->state : NULL;
}

const qa_movement_call *application_control_frames_call_swap(qa_application *app,
    const qa_movement_call *call)
{
    struct application_control_frames *owner = app ? app->control_frames : NULL;
    if (!owner) return NULL;
    const qa_movement_call *previous = owner->call;
    owner->call = call;
    return previous;
}

const qa_movement_call *application_control_frames_call_current(const qa_application *app, qa_actor_id actor)
{
    const struct application_control_frames *owner = app ? app->control_frames : NULL;
    return owner && owner->call && qa_actor_id_equal(owner->call->actor, actor) &&
        application_control_frame_current(app, actor) ? owner->call : NULL;
}

bool application_control_frames_owns(const qa_application *app, qa_actor_id actor)
{
    if (!app || !app->control_frames || actor.slot >= app->control_capacity) return false;
    const application_control_record *record = &app->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->player.actor, actor)) return false;
    application_provider *provider = source_provider((qa_application *)app, actor);
    qa_source_frame frame;
    if (!source_client(provider) || !qa_session_active_frame(app->session, provider->owner, &frame)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->actor, actor) && input->provider == provider->owner &&
        (input->turn || (input->frame_owned && input->owned_frame_number == frame.number))) return true;
    for (const control_group *group = app->control_frames->head; group; group = group->next)
        if (group->quakeworld && group->provider == provider->owner && qa_actor_id_equal(group->actor, actor)) return true;
    return false;
}

static const qa_source_frame *actor_frame(qa_application *app, qa_actor_id actor,
                                         const qa_source_frame *frames, size_t count)
{
    application_provider *source = source_provider(app, actor);
    if (!source) return NULL;
    for (size_t i = 0; i < count; ++i) if (frames[i].provider == source->owner) return &frames[i];
    return NULL;
}

typedef struct control_apply {
    qa_application *application;
    qa_actor_id actor;
    const qa_movement_command *command;
    struct application_control_turn **turn;
    qa_actor_owner arsenal;
    qa_item_id weapon;
    application_control_outcome outcome;
} control_apply;

static bool drain(qa_application *, const qa_source_frame *, size_t, bool, bool, qa_error *);
static bool nq_selected_command(qa_application *, qa_actor_id, const qa_movement_command *,
    const application_control_context *, qa_movement_command *, qa_error *);

static bool invoke_apply(void *opaque, qa_session *session, qa_error *error)
{
    control_apply *call = opaque;
    if (session != call->application->session)
        return application_fail(error, QA_ERROR_ARGUMENT, "Applied command invocation lost its session");
    if (!qa_actors_get(qa_session_actors(session), call->actor)) return true;
    call->outcome = application_control_stage_move(call->application, call->actor,
        call->command, call->turn, error);
    return call->outcome != APPLICATION_CONTROL_FAILED;
}

static application_control_outcome apply(qa_application *app, qa_actor_id actor, const qa_movement_command *command,
                    const qa_source_frame *frame, application_control_source_path path,
                    application_control_stage stage, bool retained, bool defer_postthink,
                    struct application_control_turn **turn, qa_actor_owner arsenal, qa_item_id weapon, qa_error *error)
{
    struct application_control_frames *owner = app->control_frames;
    application_control_context current = {.actor = actor, .frame = *frame, .path = path,
        .stage = stage, .retained = retained, .defer_postthink = defer_postthink};
    current.arsenal = arsenal; current.weapon = weapon;
    uint64_t host_elapsed = frame->elapsed_ns;
    (void)qa_session_advance_interval(app->session, &host_elapsed);
    current.source_elapsed_ns = source_interval(command->kind, host_elapsed);
    const application_control_context *previous = owner->current;
    owner->current = &current;
    control_input *input = &owner->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->actor, actor) && input->provider == frame->provider) {
        input->frame_owned = true; input->owned_frame_number = frame->number;
    }
    qa_movement_command selected;
    if (input->domain == CONTROL_COMMAND_UNIFIED) {
        if (!unified_command(app, actor, &input->unified, &selected, error)) {
            owner->current = previous; return false;
        }
        unified_context(&current, &input->unified);
        command = &selected;
    }
    if (input->domain == CONTROL_COMMAND_NQ_SOURCE) {
        if (!nq_selected_sources_ready(app, actor)) {
            owner->current = previous;
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Retained NQ turn lost its supported selected source execution");
        }
        current.source_nqcmd = true; current.source_command = *command;
        if (!nq_selected_command(app, actor, command, &current, &selected, error)) {
            owner->current = previous;
            return false;
        }
        command = &selected;
    }
    control_apply call = {app, actor, command, turn, arsenal, weapon, APPLICATION_CONTROL_COMPLETED};
    const qa_invocation *invocation = qa_session_current(app->session);
    bool existing_turn = stage == APPLICATION_CONTROL_PHYSICS && invocation &&
        invocation->kind == QA_INVOKE_PHYSICS && qa_actor_id_equal(invocation->actor, actor);
    bool ok = existing_turn ? invoke_apply(&call, app->session, error) :
        qa_session_invoke(app->session, actor, QA_INVOKE_PHYSICS, invoke_apply, &call, error);
    owner->current = previous;
    return ok ? call.outcome : APPLICATION_CONTROL_FAILED;
}

bool application_control_frames_prepare(void *opaque, qa_session *session, const qa_source_frame *frames,
                                         size_t count, uint64_t host_ns, qa_error *error)
{
    qa_application *app = opaque;
    if (!app || session != app->session || !app->control_frames)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command preparation lost its source owner");
    if (!application_arsenal_prepare_frame(app, frames, count, error)) return false;
    if (!application_bots_frame_at(app, frames, count, host_ns, error)) return false;
    for (uint32_t i = 0; i < app->control_capacity; ++i) {
        application_control_record *record = &app->controls[i];
        if (!record->active || !qa_actors_get(qa_session_actors(session), record->player.actor)) continue;
        application_provider *provider = source_provider(app, record->player.actor);
        control_input *input = &app->control_frames->inputs[i].value;
        if (!source_client(provider) || provider->component.clock.kind != QA_RULESET_NETQUAKE ||
            (record->player.state.kind != QA_RULESET_NETQUAKE &&
             (!qa_actor_id_equal(input->actor, record->player.actor) || input->domain != CONTROL_COMMAND_NQ_SOURCE))) continue;
        const qa_source_frame *frame = actor_frame(app, record->player.actor, frames, count);
        if (!frame) continue;
        if (!qa_actor_id_equal(input->actor, record->player.actor) && input->turn) {
            struct application_control_turn *turn = input->turn; input->turn = NULL;
            if (!application_control_turn_abort(turn, error)) return false;
        }
        (void)input_enroll(app->control_frames, i);
        if (!qa_actor_id_equal(input->actor, record->player.actor)) *input = (control_input){.actor = record->player.actor,
            .provider = provider->owner, .retained = true};
        if (input->provider != provider->owner || input->turn)
            return application_fail(error, QA_ERROR_ARGUMENT, "Retained input execution changed during admission");
        qa_movement_command command = input->seen ? input->latest :
            (qa_movement_command){.kind = QA_RULESET_NETQUAKE, .angles = record->player.command_angles};
        command.impulse = input->impulse;
        if (input->domain != CONTROL_COMMAND_NQ_SOURCE)
            command.milliseconds = (uint32_t)(frame->elapsed_ns / UINT64_C(1000000));
        application_control_outcome outcome = apply(app, record->player.actor, &command, frame,
            APPLICATION_CONTROL_NQ_TURN, APPLICATION_CONTROL_PREPARE, true, false,
            &input->turn, input->arsenal, input->weapon, error);
        if (outcome == APPLICATION_CONTROL_FAILED) return false;
        if (outcome == APPLICATION_CONTROL_SKIPPED) {
            input->source_turn_actor = record->player.actor; input->source_turn_provider = frame->provider;
            input->source_turn_frame_number = frame->number;
        }
        input->arsenal = 0; input->weapon = 0;
    }
    return drain(app, frames, count, false, true, error);
}

typedef struct command_group_call {
    qa_application *app;
    control_group *group;
    size_t index;
    bool post;
    application_control_outcome outcome;
} command_group_call;

static qa_movement_command selected_command(const application_control_record *record,
    const qa_movement_command *raw, uint64_t source_time_ns,
    const int32_t words[3], qa_vec3 axes)
{
    qa_movement_command out = {.kind = record->player.state.kind, .sequence = raw->sequence,
        .milliseconds = raw->milliseconds, .buttons = raw->buttons & 1u,
        .angles = {(float)(words[0] * 360.0 / 65536.0), (float)(words[1] * 360.0 / 65536.0),
            (float)(words[2] * 360.0 / 65536.0)}};
    uint32_t time = out.kind == QA_RULESET_Q3 ?
        (uint32_t)record->player.state.data.q3.command_time_ms : (uint32_t)(source_time_ns / UINT64_C(1000000));
    time += raw->milliseconds; memcpy(&out.server_time_ms, &time, sizeof(time));
    out.acknowledged_server_seconds = (double)out.server_time_ms / 1000.0;
    qa_movement_command source = out;
    source.forward_move = axes.x; source.side_move = axes.y; source.up_move = axes.z;
    memcpy(source.angle_words, words, sizeof(source.angle_words));
    qa_input_command_basis from = {.kind = QA_RULESET_Q3, .words = true}, to = {.kind = out.kind};
    if (out.kind == QA_RULESET_Q2_CLASSIC || out.kind == QA_RULESET_Q3) {
        to.words = to.relative = true;
        for (unsigned i = 0; i < 3; ++i) to.delta_words[i] = out.kind == QA_RULESET_Q3 ?
            record->player.state.data.q3.delta_angle_words[i] : record->player.state.data.q2.delta_angle_shorts[i];
    } else if (out.kind == QA_RULESET_Q2_RERELEASE) {
        to.relative = true; to.delta_angles = record->player.state.data.q2r.delta_angles;
    }
    qa_input_command_convert(&source, NULL, &from, &to, (qa_input_axis_rule){0}, &out);
    if (out.kind == QA_RULESET_NETQUAKE) {
        if (axes.z > 0) out.buttons |= 2u;
    } else if (out.kind == QA_RULESET_Q2_RERELEASE) {
        if (axes.z > 0) out.buttons |= 8u;
        if (axes.z < 0) out.buttons |= 16u;
        out.up_move = 0;
    }
    return out;
}

static bool q1_selected_command(qa_application *app, qa_actor_id actor,
    const qa_movement_command *raw, uint64_t source_start_ns,
    qa_movement_command *out, qa_error *error)
{
    application_control_record *record = &app->controls[actor.slot];
    qa_movement_command input = *raw, projected;
    input.up_move = raw->buttons & 2u ? 320.0f : raw->up_move;
    qa_input_command_basis from = {.kind = raw->kind}, to = {.kind = QA_RULESET_Q3, .words = true};
    qa_input_command_convert(&input, NULL, &from, &to,
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_TRUNCATE, .clamp = true,
            .minimum = -127, .maximum = 127}, &projected);
    *out = selected_command(record, raw, source_start_ns, projected.angle_words,
        qa_v3(projected.forward_move, projected.side_move, projected.up_move));
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "QW source projection lost its selected Q3 arsenal");
        out->weapon = (uint8_t)player.weapon;
    } else if (original_q3(arsenal)) {
        struct application_q3_guest *engine = q3g_engine(arsenal);
        qa_q3_player player; uint32_t slot;
        if (!engine || !engine->game || !application_q3_guest_actor_client(arsenal, actor, &slot) ||
            !qa_q3_host_source_player(engine->game->host, slot, &player, error)) return false;
        out->weapon = (uint8_t)player.weapon;
    }
    return true;
}

static bool nq_selected_command(qa_application *app, qa_actor_id actor,
    const qa_movement_command *raw, const application_control_context *context,
    qa_movement_command *out, qa_error *error)
{
    if (app->controls[actor.slot].player.state.kind == QA_RULESET_NETQUAKE) {
        *out = *raw;
        out->milliseconds = (uint32_t)(context->frame.elapsed_ns / UINT64_C(1000000));
        return true;
    }
    qa_movement_command timed = *raw;
    timed.milliseconds = (uint32_t)(context->frame.elapsed_ns / UINT64_C(1000000));
    if (!q1_selected_command(app, actor, &timed, context->frame.start_ns, out, error)) return false;
    out->impulse = raw->impulse;
    return true;
}

static bool q2_selected_command(qa_application *app, qa_actor_id actor,
    application_provider *source, const qa_movement_result *physical, const qa_movement_command *raw,
    const qa_source_command *admission, qa_movement_command *out, qa_error *error)
{
    if (physical->state.kind != raw->kind) return false;
    application_control_record *record = &app->controls[actor.slot];
    application_provider *movement = application_provider_for(app, actor, QA_ROLE_MOVEMENT, "");
    if (movement == source && record->player.state.kind == raw->kind) {
        *out = *raw;
        return true;
    }
    qa_input_command_basis from = {.kind = raw->kind, .relative = true}, to = {
        .kind = QA_RULESET_Q3, .words = true};
    if (raw->kind == QA_RULESET_Q2_CLASSIC) {
        from.words = from.wrap_words = from.repack_words = true;
        for (unsigned i = 0; i < 3; ++i) from.delta_words[i] = physical->state.data.q2.delta_angle_shorts[i];
    } else from.delta_angles = physical->state.data.q2r.delta_angles;
    qa_movement_command input = *raw, projected;
    if (raw->kind == QA_RULESET_Q2_RERELEASE)
        input.up_move = raw->buttons & 8u ? 200 : raw->buttons & 16u ? -200 : 0;
    qa_input_command_convert(&input, NULL, &from, &to,
        (qa_input_axis_rule){.quantization = QA_INPUT_AXIS_TRUNCATE, .clamp = true,
            .minimum = -127, .maximum = 127}, &projected);
    *out = selected_command(record, raw, admission->time_ns, projected.angle_words,
        qa_v3(projected.forward_move, projected.side_move, projected.up_move));
    out->impulse = raw->impulse; out->light_level = raw->light_level;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source projection lost its actual selected arsenal");
        out->weapon = (uint8_t)player.requested_weapon;
    }
    return true;
}

static bool unified_q2_source(const control_unified *receipt, qa_ruleset_id source_kind,
    const qa_movement_command *selected, uint64_t elapsed_ns, qa_movement_command *out, qa_error *error)
{
    const qa_unified_movement *movement = &receipt->movement;
    double forward, side, up, buttons;
    qa_movement_command input = {.kind = movement->kind};
    qa_input_command_basis from = {.kind = movement->kind}, to = {
        .kind = source_kind, .words = source_kind == QA_RULESET_Q2_CLASSIC, .wrap_words = true};
    if (movement->kind == QA_RULESET_Q2_CLASSIC || movement->kind == QA_RULESET_Q3) {
        const double *words = movement->kind == QA_RULESET_Q3 ? movement->data.q3.angle_words : movement->data.q2.angle_shorts;
        for (unsigned i = 0; i < 3; ++i) input.angle_words[i] = (int32_t)words[i];
        from.words = true;
        if (movement->kind == QA_RULESET_Q3) {
            forward = movement->data.q3.forward; side = movement->data.q3.right; up = movement->data.q3.up;
            buttons = movement->data.q3.buttons;
        } else {
            forward = movement->data.q2.forward; side = movement->data.q2.side; up = movement->data.q2.up;
            buttons = movement->data.q2.buttons;
        }
    } else {
        qa_unified_vec3 aim;
        if (movement->kind == QA_RULESET_NETQUAKE) {
            aim = movement->data.nq.angles; forward = movement->data.nq.forward;
            side = movement->data.nq.side; up = movement->data.nq.up; buttons = movement->data.nq.buttons;
        } else if (movement->kind == QA_RULESET_QUAKEWORLD) {
            aim = movement->data.qw.angles; forward = movement->data.qw.forward;
            side = movement->data.qw.side; up = movement->data.qw.up; buttons = movement->data.qw.buttons;
        } else {
            aim = movement->data.q2r.angles; forward = movement->data.q2r.forward;
            side = movement->data.q2r.side; buttons = movement->data.q2r.buttons;
            up = (uint32_t)buttons & 8u ? 200 : (uint32_t)buttons & 16u ? -200 : 0;
        }
        input.angles = qa_v3((float)aim.x, (float)aim.y, (float)aim.z);
    }
    uint32_t button_word = (uint32_t)buttons;
    if ((movement->kind == QA_RULESET_NETQUAKE || movement->kind == QA_RULESET_QUAKEWORLD) && (button_word & 2u)) up = 320;
    uint64_t duration = movement->kind == QA_RULESET_NETQUAKE ? elapsed_ns / UINT64_C(1000000) : selected->milliseconds;
    qa_movement_command command = {.kind = source_kind, .sequence = receipt->sequence,
        .milliseconds = (uint8_t)duration, .buttons = button_word & 1u, .impulse = selected->impulse};
    if (receipt->has_arsenal && receipt->has_impulse) command.impulse = receipt->impulse;
    qa_input_move_intent moves = {forward, side, up};
    qa_movement_command converted;
    qa_input_command_convert(&input, &moves, &from, &to,
        (qa_input_axis_rule){.quantization = source_kind == QA_RULESET_Q2_CLASSIC ?
            QA_INPUT_AXIS_SHORT : QA_INPUT_AXIS_EXACT}, &converted);
    command.forward_move = converted.forward_move; command.side_move = converted.side_move;
    command.up_move = converted.up_move;
    if (source_kind == QA_RULESET_Q2_CLASSIC) {
        for (unsigned i = 0; i < 3; ++i) command.angle_words[i] =
            converted.angle_words[i] <= INT16_MAX ? converted.angle_words[i] : converted.angle_words[i] - 65536;
        command.light_level = selected->light_level;
    } else {
        if (!qa_vec_finite(converted.angles) || !isfinite(converted.forward_move) ||
            !isfinite(converted.side_move) || !isfinite(converted.up_move))
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified Source Q2 command exceeds its real float fields");
        command.angles = converted.angles;
        if (up > 0) command.buttons |= 8u;
        if (up < 0) command.buttons |= 16u;
        command.up_move = 0;
        if (movement->kind == QA_RULESET_Q2_RERELEASE) command.server_frame = (int32_t)movement->data.q2r.server_frame;
    }
    *out = command; return true;
}

static application_control_outcome qw_foreign_slice(command_group_call *call,
    application_control_context *current, qa_movement_command raw, uint32_t maximum, qa_error *error)
{
    if (raw.milliseconds > maximum) {
        raw.milliseconds /= 2;
        application_control_outcome outcome = qw_foreign_slice(call, current, raw, maximum, error);
        if (outcome != APPLICATION_CONTROL_COMPLETED) return outcome;
        raw.impulse = 0;
        return !qa_actors_get(qa_session_actors(call->app->session), call->group->actor)
            ? APPLICATION_CONTROL_COMPLETED : qw_foreign_slice(call, current, raw, maximum, error);
    }
    qa_application *app = call->app; qa_actor_id actor = call->group->actor;
    application_source_input_scope scope = {0};
    current->source_elapsed_ns = (uint64_t)raw.milliseconds * UINT64_C(1000000);
    current->source_command = raw;
    bool ok = application_control_source_input(app, actor, &app->controls[actor.slot].player.state,
        &raw, NULL, &scope, true, true, current->source_elapsed_ns, error);
    application_control_outcome outcome = APPLICATION_CONTROL_COMPLETED;
    if (ok && qa_actors_get(qa_session_actors(app->session), actor)) {
        current->source_command = raw;
        qa_movement_command selected;
        ok = q1_selected_command(app, actor, &raw, current->command.time_ns, &selected, error);
        if (ok) {
            outcome = application_control_stage_move(app, actor, &selected, NULL, error);
            ok = outcome != APPLICATION_CONTROL_FAILED;
        }
    }
    if (ok && outcome == APPLICATION_CONTROL_COMPLETED &&
        qa_actors_get(qa_session_actors(app->session), actor))
        ok = application_control_source_input(app, actor, &app->controls[actor.slot].player.state,
            &raw, NULL, &scope, false, true, current->source_elapsed_ns, error);
    qa_error cleanup = {0};
    if (!application_control_source_abort(&scope, &cleanup)) {
        if (error) *error = cleanup;
        application_fault(app, &cleanup);
        return APPLICATION_CONTROL_FAILED;
    }
    return application_control_finish(app, actor, ok ? outcome : APPLICATION_CONTROL_FAILED,
        "control/qw-slice", error);
}

static application_control_outcome qw_foreign_command(command_group_call *call,
    application_control_context *current, qa_error *error)
{
    qa_application *app = call->app; qa_actor_id actor = call->group->actor;
    qa_movement_command raw = call->group->commands[call->index];
    qa_movement_profile source = qa_movement_profile_default(QA_RULESET_QUAKEWORLD);
    uint32_t maximum = source.data.qw.maximum_command_ms;
    uint32_t elapsed = raw.milliseconds, slices = 1;
    bool active = false;
    for (size_t i = 0; i < app->provider_count; ++i)
        active |= app->providers[i]->attached && !app->providers[i]->close_pending &&
            application_qc_control_input_active(app->providers[i]);
    if (active) {
        while (elapsed > maximum) { elapsed /= 2; slices *= 2; }
        elapsed *= slices;
    }
    application_source_input_scope scope = {0};
    current->source_input_applied = true; current->source_command = raw;
    current->source_elapsed_ns = (uint64_t)elapsed * UINT64_C(1000000);
    bool ok = application_control_source_input(app, actor, &app->controls[actor.slot].player.state,
        &raw, NULL, &scope, true, false, current->source_elapsed_ns, error);
    application_control_outcome outcome = APPLICATION_CONTROL_COMPLETED;
    if (ok && qa_actors_get(qa_session_actors(app->session), actor)) {
        outcome = qw_foreign_slice(call, current, raw, maximum, error);
        ok = outcome != APPLICATION_CONTROL_FAILED;
    }
    if (ok && outcome == APPLICATION_CONTROL_COMPLETED &&
        qa_actors_get(qa_session_actors(app->session), actor))
        ok = application_control_source_input(app, actor, &app->controls[actor.slot].player.state,
            &raw, NULL, &scope, false, false, (uint64_t)elapsed * UINT64_C(1000000), error);
    qa_error cleanup = {0};
    if (!application_control_source_abort(&scope, &cleanup)) {
        if (error) *error = cleanup;
        application_fault(app, &cleanup);
        return APPLICATION_CONTROL_FAILED;
    }
    return application_control_finish(app, actor, ok ? outcome : APPLICATION_CONTROL_FAILED,
        "control/qw-command", error);
}

typedef struct native_q2_command_call {
    qa_application *app;
    application_provider *source, *movement, *arsenal;
    application_control_context *current;
    application_control_outcome outcome;
} native_q2_command_call;

static bool native_q2_command_current(void *context, qa_actor_id actor)
{
    native_q2_command_call *call = context;
    qa_application *app = call->app;
    const application_control_context *current = call->current;
    return app->state == QA_APPLICATION_RUNNING && app->control_frames->current == current &&
        current->source_q2cmd && current->command_only && qa_actor_id_equal(current->actor, actor) &&
        current->command.provider == call->source->owner && source_provider(app, actor) == call->source &&
        native_q2_raw_source_client(call->source, actor) &&
        application_provider_for(app, actor, QA_ROLE_MOVEMENT, "") == call->movement &&
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == call->arsenal &&
        actor.slot < app->control_capacity && qa_actors_get(qa_session_actors(app->session), actor) &&
        app->controls[actor.slot].active && !app->controls[actor.slot].retired &&
        qa_actor_id_equal(app->controls[actor.slot].player.actor, actor);
}

static bool native_q2_command_move(void *context, const qa_movement_input *input,
    const qa_movement_services *services, qa_movement_result *out, qa_error *error)
{
    native_q2_command_call *call = context;
    (void)services;
    if (!input || !out || !native_q2_command_current(call, input->actor) ||
        input->command.sequence != call->current->source_command.sequence ||
        input->state.kind != call->current->source_command.kind || call->movement == call->source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove lost its selected Source command stage");
    qa_movement_result physical = {.state = input->state};
    qa_movement_command selected;
    if (!q2_selected_command(call->app, input->actor, call->source, &physical,
            &input->command, &call->current->command, &selected, error)) return false;
    uint64_t previous_elapsed = call->current->source_elapsed_ns;
    call->current->source_elapsed_ns = input->elapsed_ns;
    application_control_outcome moved = application_control_stage_move(call->app, input->actor, &selected, NULL, error);
    call->current->source_elapsed_ns = previous_elapsed;
    call->outcome = moved;
    if (moved != APPLICATION_CONTROL_COMPLETED) return false;
    if (!qa_actors_get(qa_session_actors(call->app->session), input->actor)) {
        qa_movement_result_free(out);
        *out = (qa_movement_result){.status = QA_MOVEMENT_ACTOR_REMOVED,
            .actor = input->actor, .command_sequence = selected.sequence};
        return true;
    }
    application_control_record *record = &call->app->controls[input->actor.slot];
    const qa_movement_result *result = &record->result;
    if (!native_q2_command_current(call, input->actor) || record->moving ||
        result->status != QA_MOVEMENT_ACTIVE || !qa_actor_id_equal(result->actor, input->actor) ||
        result->command_sequence != selected.sequence || result->contact_count > result->contact_capacity ||
        (result->contact_count && !result->contacts) ||
        result->contact_count > SIZE_MAX / sizeof(*result->contacts))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove lacks its completed selected movement result");
    qa_movement_contact *contacts = NULL;
    if (result->contact_count) {
        contacts = malloc(result->contact_count * sizeof(*contacts));
        if (!contacts) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected Native Q2 movement contacts");
        memcpy(contacts, result->contacts, result->contact_count * sizeof(*contacts));
    }
    qa_movement_result_free(out);
    *out = *result;
    out->contacts = contacts; out->contact_capacity = out->contact_count;
    return true;
}

static bool native_q2_command_arsenal(void *context, qa_actor_id actor,
    const qa_movement_command *raw, uint64_t time_ns, qa_error *error)
{
    native_q2_command_call *call = context;
    if (!raw || !native_q2_command_current(call, actor) || call->movement != call->source ||
        !call->arsenal || call->arsenal == call->source || time_ns != call->current->command.time_ns ||
        raw->kind != call->current->source_command.kind ||
        raw->sequence != call->current->source_command.sequence || !q2_raw_valid(raw))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 weapon dispatcher lost its selected Source command stage");
    return application_control_native_q2_weapon_step(call->source, actor, raw, time_ns, error);
}

static bool end_skipped_command(qa_application *app, qa_actor_id actor,
    const application_control_context *current, qa_error *error)
{
    if (!current->source_q2cmd || !qa_actors_get(qa_session_actors(app->session), actor)) return true;
    application_provider *source = source_provider(app, actor);
    if (!source || source->kind != APPLICATION_PROVIDER_Q2) return true;
    return qa_q2_player_movement_complete(source->state.q2, actor,
        NULL, &current->source_command, false, false, error);
}

static bool apply_command_group(void *opaque, qa_session *session, const qa_source_command *command, qa_error *error)
{
    command_group_call *call = opaque;
    qa_application *app = call->app; control_group *group = call->group;
    if (session != app->session || group->provider != command->provider ||
        !qa_actor_id_equal(group->actor, command->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Command group lost its genuine source admission");
    application_snapshot_mutated(app);
    struct application_control_frames *owner = app->control_frames;
    const application_control_context *previous = owner->current;
    application_control_context current = {.actor = group->actor, .path = group->quakeworld
        ? APPLICATION_CONTROL_QW_GROUP : APPLICATION_CONTROL_MIXED, .stage = APPLICATION_CONTROL_COMMAND,
        .command_only = true, .command = *command, .defer_postthink = group->quakeworld};
    if (group->domain == CONTROL_COMMAND_QW_SOURCE) {
        current.source_qwcmd = true; current.source_command = group->commands[call->index];
        current.source_elapsed_ns = (uint64_t)current.source_command.milliseconds * UINT64_C(1000000);
    }
    bool ok = true;
    application_control_outcome outcome = APPLICATION_CONTROL_COMPLETED;
    if (group->domain == CONTROL_COMMAND_UNIFIED) {
        qa_movement_command selected;
        owner->current = &current;
        unified_context(&current, &group->unified);
        ok = unified_command(app, group->actor, &group->unified, &selected, error);
        if (ok) {
            current.source_elapsed_ns = source_interval(selected.kind, command->host_elapsed_ns);
            if (selected.kind != QA_RULESET_NETQUAKE)
                current.source_elapsed_ns = (uint64_t)selected.milliseconds * UINT64_C(1000000);
            application_provider *source = source_provider(app, group->actor);
            bool physical_q2 = source && source->kind == APPLICATION_PROVIDER_Q2 &&
                source == application_world_provider(app, QA_ROLE_ENTITIES, "") && source->component.command_actor &&
                source->component.command_actor(source->component.state, app->session, group->actor);
            qa_movement_result physical;
            bool run_pmove = true, source_movement = false, was_grounded = false;
            if (physical_q2) {
                ok = qa_q2_player_movement_read(source->state.q2, group->actor, &physical, NULL, error) &&
                    unified_q2_source(&group->unified, physical.state.kind, &selected,
                        current.source_elapsed_ns, &current.source_command, error) &&
                    qa_q2_player_movement_prepare(source->state.q2, group->actor,
                        &current.source_command, &physical, &run_pmove, error);
                was_grounded = ok && physical.ground.hit != QA_TRACE_HIT_NONE;
                current.source_q2cmd = true;
                source_movement = application_provider_for(app, group->actor, QA_ROLE_MOVEMENT, "") == source;
                if (ok && source_movement && run_pmove) {
                    ok = unified_command(app, group->actor, &group->unified, &selected, error);
                }
            }
            bool cutscene = app->controls[group->actor.slot].player.cutscene;
            if (ok && run_pmove && (!physical_q2 || !cutscene)) {
                outcome = application_control_stage_move(app, group->actor, &selected, NULL, error);
                ok = outcome != APPLICATION_CONTROL_FAILED;
            }
            if (ok && outcome == APPLICATION_CONTROL_COMPLETED && physical_q2 && run_pmove &&
                qa_actors_get(qa_session_actors(session), group->actor)) {
                application_control_record *record = &app->controls[group->actor.slot];
                if (source_provider(app, group->actor) != source || !record->active || record->moving ||
                    record->retired || !qa_actor_id_equal(record->player.actor, group->actor) ||
                    (!cutscene && (!qa_actor_id_equal(record->result.actor, group->actor) ||
                        record->result.command_sequence != group->unified.sequence)))
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 Source completion lost its selected turn");
                else ok = qa_q2_player_movement_complete(source->state.q2, group->actor,
                    cutscene ? NULL : &record->result, &current.source_command, !cutscene && source_movement, was_grounded, error);
                if (ok && !cutscene) ok = qa_q2_player_after_movement(source->state.q2, group->actor, false, error);
            }
        }
    } else if (group->domain == CONTROL_COMMAND_Q2_SOURCE) {
        application_provider *source = source_provider(app, group->actor);
        current.source_q2cmd = true; current.source_command = group->commands[call->index];
        current.source_elapsed_ns = (uint64_t)current.source_command.milliseconds * UINT64_C(1000000);
        current.arsenal = call->index == 0 ? group->arsenal : 0;
        current.weapon = call->index == 0 ? group->weapon : 0;
        current.retained = true;
        owner->current = &current;
        if (native_q2_raw_source_client(source, group->actor)) {
            native_q2_command_call native = {.app = app, .source = source, .current = &current,
                .movement = application_provider_for(app, group->actor, QA_ROLE_MOVEMENT, ""),
                .arsenal = application_provider_for(app, group->actor, QA_ROLE_ARSENAL, ""),
                .outcome = APPLICATION_CONTROL_COMPLETED};
            application_native_q2_input_stage stage = {.context = &native, .actor = group->actor,
                .time_ns = command->time_ns, .current = native_q2_command_current, .move = native_q2_command_move,
                .arsenal = native_q2_command_arsenal};
            ok = application_native_q2_input_think(source, group->actor, &current.source_command, &stage, error);
            if (native.outcome == APPLICATION_CONTROL_SKIPPED) {
                outcome = application_control_finish(app, group->actor, native.outcome,
                    "control/native-q2-command", error);
                ok = outcome != APPLICATION_CONTROL_FAILED;
            }
        } else {
            qa_movement_command selected;
            qa_movement_result physical;
            bool run_pmove = false, was_grounded = false;
            bool source_movement = application_provider_for(app, group->actor, QA_ROLE_MOVEMENT, "") == source;
            ok = source && source->kind == APPLICATION_PROVIDER_Q2 &&
                qa_q2_player_movement_prepare(source->state.q2, group->actor,
                    &current.source_command, &physical, &run_pmove, error);
            was_grounded = ok && physical.ground.hit != QA_TRACE_HIT_NONE;
            bool cutscene = ok && app->controls[group->actor.slot].player.cutscene;
            if (ok && run_pmove && !cutscene && qa_actors_get(qa_session_actors(session), group->actor)) {
                ok = q2_selected_command(app, group->actor, source, &physical,
                    &current.source_command, command, &selected, error);
                if (ok) {
                    outcome = application_control_stage_move(app, group->actor, &selected, NULL, error);
                    ok = outcome != APPLICATION_CONTROL_FAILED;
                }
            }
            if (ok && outcome == APPLICATION_CONTROL_COMPLETED && run_pmove &&
                qa_actors_get(qa_session_actors(session), group->actor)) {
                application_control_record *record = &app->controls[group->actor.slot];
                if (source_provider(app, group->actor) != source || !record->active || record->moving ||
                    record->retired || !qa_actor_id_equal(record->player.actor, group->actor) ||
                    (!cutscene && (!qa_actor_id_equal(record->result.actor, group->actor) ||
                        record->result.command_sequence != selected.sequence)))
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source completion lost its selected phase");
                else ok = qa_q2_player_movement_complete(source->state.q2, group->actor,
                    cutscene ? NULL : &record->result, &current.source_command,
                    !cutscene && source_movement, was_grounded, error);
                if (ok && !cutscene)
                    ok = qa_q2_player_after_movement(source->state.q2, group->actor, false, error);
            }
        }
    } else if (group->domain == CONTROL_COMMAND_Q3_SOURCE) {
        current.source_guestcmd = true; current.retained = true;
        current.source_elapsed_ns = source_interval(app->controls[group->actor.slot].player.state.kind,
            command->host_elapsed_ns);
        current.arsenal = group->arsenal; current.weapon = group->weapon;
        owner->current = &current;
        ok = application_arsenal_guest_source_command(app, group->actor, &group->commands[call->index], error);
    } else if (call->post) {
        current.defer_postthink = false; current.arsenal = 0; current.weapon = 0;
        owner->current = &current;
        ok = application_control_group_post(app, group->actor, error);
    } else if (qa_actors_get(qa_session_actors(session), group->actor)) {
        current.retained = group->quakeworld && call->index != 0;
        if (!current.source_qwcmd)
            current.source_elapsed_ns = source_interval(group->commands[call->index].kind, command->host_elapsed_ns);
        current.arsenal = call->index == 0 ? group->arsenal : 0;
        current.weapon = call->index == 0 ? group->weapon : 0;
        owner->current = &current;
        control_input *input = &owner->inputs[group->actor.slot].value;
        qa_source_frame frame;
        if (qa_actor_id_equal(input->actor, group->actor) && input->provider == group->provider &&
            qa_session_active_frame(session, group->provider, &frame)) {
            input->frame_owned = true; input->owned_frame_number = frame.number;
        }
        if (group->domain == CONTROL_COMMAND_QW_SOURCE &&
            app->controls[group->actor.slot].player.state.kind != QA_RULESET_QUAKEWORLD) {
            outcome = qw_foreign_command(call, &current, error);
            ok = outcome != APPLICATION_CONTROL_FAILED;
        } else {
            outcome = application_control_stage_move(app, group->actor,
                &group->commands[call->index], NULL, error);
            ok = outcome != APPLICATION_CONTROL_FAILED;
        }
    }
    if (!ok || outcome == APPLICATION_CONTROL_SKIPPED) {
        qa_error cleanup = {0};
        if (!end_skipped_command(app, group->actor, &current, &cleanup)) {
            if (error) *error = cleanup;
            application_fault(app, &cleanup);
            outcome = APPLICATION_CONTROL_FAILED;
        } else if (!ok)
            outcome = application_control_finish(app, group->actor, false, "control/source-command", error);
    }
    call->outcome = outcome;
    owner->current = previous;
    return outcome != APPLICATION_CONTROL_FAILED;
}

static bool execute_original_q3_group(qa_application *app, control_group *group, qa_error *error)
{
    command_group_call call = {app, group, 0, false, APPLICATION_CONTROL_COMPLETED};
    application_operation previous = app->operation;
    app->operation = APPLICATION_ADVANCING;
    bool ok = qa_session_command_call(app->session, group->provider, group->actor, 0,
        apply_command_group, &call, error);
    app->operation = previous;
    if (ok && qa_actors_get(qa_session_actors(app->session), group->actor)) {
        application_control_record *record = &app->controls[group->actor.slot];
        record->player.command_sequence = group->commands[0].sequence; record->command_seen = true;
    }
    if (ok && qa_session_safe(app->session) && qa_world_idle(app->world) &&
        qa_modes_idle(app->modes) && qa_combat_idle(app->combat))
        ok = application_q3_components_drain(app->components, error);
    return ok;
}

static bool drain(qa_application *app, const qa_source_frame *frames, size_t count,
                    bool quakeworld, bool before, qa_error *error)
{
    struct application_control_frames *owner = app->control_frames;
    if (owner->draining) return application_fail(error, QA_ERROR_ARGUMENT, "Source command drain reentered");
    owner->draining = true;
    bool ok = true;
    while (ok) {
        control_group **link = &owner->head;
        while (*link) {
            control_group *candidate = *link;
            bool candidate_live = qa_actors_get(qa_session_actors(app->session), candidate->actor) != NULL;
            application_provider *provider = candidate_live ? source_provider(app, candidate->actor) : NULL;
            bool later_source = provider && (provider->component.clock.kind == QA_RULESET_Q3 ||
                provider->component.clock.kind == QA_RULESET_QUAKEWORLD) &&
                !actor_frame(app, candidate->actor, frames, count) && qa_session_frame_pending(app->session, provider->owner);
            if (!candidate_live || (candidate->quakeworld == quakeworld && candidate->before_source == before &&
                !later_source)) break;
            link = &candidate->next;
        }
        if (!*link) break;
        control_group *group = *link;
        const qa_source_frame *frame = actor_frame(app, group->actor, frames, count);
        bool live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
        *link = group->next;
        if (owner->tail == group) owner->tail = NULL;
        owner->touched_count = 0;
        application_provider *provider = live ? source_provider(app, group->actor) : NULL;
        if (live && (!provider || group->provider != provider->owner || (frame && group->provider != frame->provider)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Pending input execution owner changed");
        bool skipped = false;
        uint64_t host_elapsed = frame ? frame->elapsed_ns : 0;
        (void)qa_session_advance_interval(app->session, &host_elapsed);
        for (size_t i = 0; ok && live && !skipped && i < group->count; ++i) {
            if (group->domain == CONTROL_COMMAND_UNIFIED && provider->kind == APPLICATION_PROVIDER_Q3) {
                qa_q3_usercmd raw;
                ok = unified_q3_source(app, group->actor, provider, &group->unified, &raw, error);
                if (ok) {
                    owner->q3_group = group;
                    application_control_outcome outcome = application_control_q3_client_think(provider, group->actor, &raw, error);
                    ok = outcome != APPLICATION_CONTROL_FAILED;
                    skipped = outcome == APPLICATION_CONTROL_SKIPPED;
                    owner->q3_group = NULL;
                }
                live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
                if (ok && live) {
                    app->controls[group->actor.slot].player.command_sequence = group->unified.sequence;
                    app->controls[group->actor.slot].command_seen = true;
                }
                continue;
            }
            if (group->domain == CONTROL_COMMAND_Q3_SOURCE && provider->kind == APPLICATION_PROVIDER_Q3) {
                qa_q3_usercmd raw = q3_source_command(&group->commands[i], true);
                owner->q3_group = group;
                application_control_outcome outcome = application_control_q3_client_think(provider, group->actor, &raw, error);
                ok = outcome != APPLICATION_CONTROL_FAILED;
                skipped = outcome == APPLICATION_CONTROL_SKIPPED;
                owner->q3_group = NULL;
                live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
                if (ok && live) {
                    application_control_record *record = &app->controls[group->actor.slot];
                    record->player.command_sequence = group->commands[i].sequence; record->command_seen = true;
                }
                continue;
            }
            qa_movement_command timing = group->commands[i];
            if (group->domain == CONTROL_COMMAND_UNIFIED &&
                !unified_command(app, group->actor, &group->unified, &timing, error)) { ok = false; break; }
            uint64_t elapsed = timing.kind == QA_RULESET_NETQUAKE
                ? source_interval(group->commands[i].kind, host_elapsed)
                : (uint64_t)timing.milliseconds * UINT64_C(1000000);
            command_group_call call = {app, group, i, false, APPLICATION_CONTROL_COMPLETED};
            ok = qa_session_command_call(app->session, group->provider, group->actor, elapsed,
                apply_command_group, &call, error);
            skipped = call.outcome == APPLICATION_CONTROL_SKIPPED;
            live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
            if (ok && live && (group->domain == CONTROL_COMMAND_Q3_SOURCE || group->domain == CONTROL_COMMAND_Q2_SOURCE ||
                group->domain == CONTROL_COMMAND_UNIFIED)) {
                application_control_record *record = &app->controls[group->actor.slot];
                record->player.command_sequence = group->commands[i].sequence; record->command_seen = true;
            }
        }
        if (ok && live && !skipped && quakeworld) {
            uint64_t elapsed = (uint64_t)group->commands[group->count - 1].milliseconds * UINT64_C(1000000);
            command_group_call call = {app, group, group->count - 1, true, APPLICATION_CONTROL_COMPLETED};
            ok = qa_session_command_call(app->session, group->provider, group->actor, elapsed,
                apply_command_group, &call, error);
        }
        if (ok && live && group->domain == CONTROL_COMMAND_QW_SOURCE) {
            application_control_record *record = &app->controls[group->actor.slot];
            record->player.command_sequence = group->commands[group->count - 1].sequence;
            record->command_seen = true;
        }
        free(group);
        owner->touched_count = 0;
    }
    owner->tail = NULL;
    for (control_group *group = owner->head; group; group = group->next) owner->tail = group;
    owner->draining = false;
    return ok;
}

bool application_control_frames_commands(void *opaque, qa_session *session, const qa_source_frame *frames,
                                          size_t count, uint64_t host_ns, qa_error *error)
{
    (void)host_ns; qa_application *app = opaque;
    if (!app || session != app->session || !app->control_frames)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command drain lost its source owner");
    application_provider *map = application_world_provider(app, QA_ROLE_ENTITIES, "");
    bool native_q1 = map && map->kind == APPLICATION_PROVIDER_Q1 &&
        map->component.clock.kind == QA_RULESET_NETQUAKE;
    bool due = false;
    for (size_t i = 0; i < count; ++i) due |= native_q1 && frames[i].provider == map->owner;
    bool applications_active = false;
    if (due) for (size_t i = 0; i < app->provider_count; ++i) {
        const application_provider *provider = app->providers[i];
        applications_active |= provider->attached && !provider->close_pending &&
            application_qc_control_input_active(provider);
    }
    if (due) for (uint32_t i = 0; i < app->control_capacity; ++i) {
        application_control_record *record = &app->controls[i];
        if (!record->active || record->player.state.kind == QA_RULESET_NETQUAKE ||
            (qa_actor_id_equal(app->control_frames->inputs[i].value.actor, record->player.actor) &&
             app->control_frames->inputs[i].value.domain == CONTROL_COMMAND_NQ_SOURCE) ||
            !qa_q1_player_source_present(map->state.q1, record->player.actor)) continue;
        const qa_movement_command *received = NULL;
        for (const control_group *group = app->control_frames->head; group; group = group->next)
            if (!group->bot && qa_actor_id_equal(group->actor, record->player.actor))
                received = &group->commands[group->count - 1];
        control_input *input = &app->control_frames->inputs[i].value;
        qa_movement_command unified_received;
        if (received && input->domain == CONTROL_COMMAND_UNIFIED) {
            if (!unified_command(app, record->player.actor, &input->unified, &unified_received, error)) return false;
            received = &unified_received;
        }
        if (received && (!qa_actor_id_equal(input->actor, record->player.actor) || !input->seen))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source preparation lost its received command");
        if (received && applications_active) {
            qa_source_frame actual;
            if (!qa_session_active_frame(app->session, map->owner, &actual))
                return application_fail(error, QA_ERROR_ARGUMENT, "Deferred Q1 source input lost its real map frame");
            (void)input_enroll(app->control_frames, i);
            input->q1_source_actor = record->player.actor; input->q1_source_provider = map->owner;
            input->q1_source_frame_number = actual.number; input->q1_source_sequence = received->sequence;
            input->q1_source_deferred = true;
            continue;
        }
        if (!application_control_q1_source_prethink(app, record->player.actor, received, error)) return false;
    }
    return drain(app, frames, count, false, false, error);
}

bool application_control_frames_actor(void *opaque, qa_session *session, qa_actor_id actor,
                                       const qa_source_frame *frame, bool *handled, qa_error *error)
{
    qa_application *app = opaque;
    if (!app || session != app->session || !handled || !frame)
        return application_fail(error, QA_ERROR_ARGUMENT, "Controlled actor turn lost its source owner");
    application_provider *provider = source_provider(app, actor);
    bool reserved = false;
    if (provider && provider->kind == APPLICATION_PROVIDER_QC && !provider->state.qc.qualified &&
        !application_qc_control_reserved(provider, actor, &reserved, error)) return false;
    bool physical_nq = provider && (provider->kind == APPLICATION_PROVIDER_Q1 ||
        (provider->kind == APPLICATION_PROVIDER_QC && !provider->state.qc.qualified)) &&
        actor.slot < app->control_capacity && app->controls[actor.slot].active &&
        qa_actor_id_equal(app->controls[actor.slot].player.actor, actor) &&
        (app->controls[actor.slot].player.state.kind == QA_RULESET_NETQUAKE ||
         (qa_actor_id_equal(app->control_frames->inputs[actor.slot].value.actor, actor) &&
          app->control_frames->inputs[actor.slot].value.domain == CONTROL_COMMAND_NQ_SOURCE)) &&
        provider->component.clock.kind == QA_RULESET_NETQUAKE &&
        provider->component.command_actor &&
        provider->component.command_actor(provider->component.state, session, actor);
    *handled = reserved || physical_nq || application_control_frames_owns(app, actor);
    if (!*handled) return true;
    if (physical_nq && frame->provider != provider->owner) return true;
    control_input *input = &app->control_frames->inputs[actor.slot].value;
    if (qa_actor_id_equal(input->source_turn_actor, actor) &&
        input->source_turn_provider == frame->provider && input->source_turn_frame_number == frame->number)
        return true;
    bool ready = true;
    if (input->turn && qa_actor_id_equal(input->actor, actor)) {
        if (input->provider != frame->provider)
            ready = application_fail(error, QA_ERROR_ARGUMENT, "Prepared input execution owner changed before physics");
        else ready = application_control_turn_resume(input->turn, error);
    }
    if (ready && provider && provider->kind == APPLICATION_PROVIDER_QC)
        ready = application_qc_control_before_actor(provider, actor, frame, error);
    if (!ready) {
        struct application_control_turn *turn = input->turn; input->turn = NULL;
        qa_error cleanup = {0};
        if (!application_control_turn_abort(turn, &cleanup)) {
            if (error) *error = cleanup;
            application_fault(app, &cleanup);
            return false;
        }
        return application_control_finish(app, actor, APPLICATION_CONTROL_FAILED,
            "control/source-resume", error) != APPLICATION_CONTROL_FAILED;
    }
    if (!qa_actors_get(qa_session_actors(session), actor)) {
        struct application_control_turn *turn = input->turn; input->turn = NULL;
        bool okay = application_control_turn_abort(turn, error);
        *input = (control_input){0};
        return okay;
    }
    if (!input->turn || !qa_actor_id_equal(input->actor, actor)) {
        (void)input_enroll(app->control_frames, actor.slot);
        input->source_turn_actor = actor; input->source_turn_provider = frame->provider;
        input->source_turn_frame_number = frame->number;
        return true;
    }
    qa_movement_command command = input->seen ? input->latest : (qa_movement_command){.kind = QA_RULESET_NETQUAKE};
    command.impulse = input->impulse;
    application_control_outcome outcome = apply(app, actor, &command, frame, APPLICATION_CONTROL_NQ_TURN,
        APPLICATION_CONTROL_PHYSICS, true, false, &input->turn, 0, 0, error);
    if (outcome != APPLICATION_CONTROL_FAILED) {
        input->impulse = 0;
        if (input->domain == CONTROL_COMMAND_UNIFIED) {
            input->unified.movement.data.nq.impulse = 0;
            input->unified.impulse = 0;
        }
        if (outcome == APPLICATION_CONTROL_COMPLETED &&
            input->domain == CONTROL_COMMAND_NQ_SOURCE && input->seen &&
            qa_actors_get(qa_session_actors(session), actor)) {
            app->controls[actor.slot].player.command_sequence = input->sequence;
            app->controls[actor.slot].command_seen = true;
        }
        (void)input_enroll(app->control_frames, actor.slot);
        input->source_turn_actor = actor; input->source_turn_provider = frame->provider;
        input->source_turn_frame_number = frame->number;
    }
    if (!qa_actors_get(qa_session_actors(session), actor)) *input = (control_input){0};
    return outcome != APPLICATION_CONTROL_FAILED;
}

bool application_control_frames_end(void *opaque, qa_session *session, const qa_source_frame *frames,
                                     size_t count, uint64_t host_ns, qa_error *error)
{
    (void)host_ns; qa_application *app = opaque;
    if (!app || session != app->session || !app->control_frames)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command completion lost its source owner");
    for (control_input_slot *slot = app->control_frames->first_input; slot; slot = slot->next) {
        control_input *input = &slot->value;
        if (input->turn) {
            struct application_control_turn *turn = input->turn; input->turn = NULL;
            if (!application_control_turn_abort(turn, error)) return false;
        }
        if (input->actor.registry && !qa_actors_get(qa_session_actors(session), input->actor)) *input = (control_input){0};
        input->frame_owned = false; input->owned_frame_number = 0;
        input->source_turn_actor = (qa_actor_id){0}; input->source_turn_provider = 0;
        input->source_turn_frame_number = 0;
        input->q1_source_actor = (qa_actor_id){0}; input->q1_source_provider = 0;
        input->q1_source_frame_number = 0; input->q1_source_sequence = 0;
        input->q1_source_deferred = false;
    }
    bool ok = drain(app, frames, count, true, false, error);
    for (control_input_slot *slot = app->control_frames->first_input; slot; slot = slot->next) {
        slot->value.frame_owned = false; slot->value.owned_frame_number = 0;
    }
    inputs_prune(app->control_frames);
    return ok;
}

static bool command_fields(qa_source_save_io *io, qa_movement_command *command)
{
    uint32_t kind = command->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_RULESET_Q3 ||
        !qa_source_save_u64(io, &command->sequence) || !qa_source_save_u32(io, &command->milliseconds) ||
        !qa_source_save_i32(io, &command->server_time_ms) || !qa_source_save_i32(io, &command->server_frame) ||
        !qa_source_save_f64(io, &command->acknowledged_server_seconds) || !qa_source_save_vec3(io, &command->angles)) return false;
    for (unsigned i = 0; i < 3; ++i) if (!qa_source_save_i32(io, &command->angle_words[i])) return false;
    if (!qa_source_save_f32(io, &command->forward_move) || !qa_source_save_f32(io, &command->side_move) ||
        !qa_source_save_f32(io, &command->up_move) || !qa_source_save_u32(io, &command->buttons) ||
        !qa_source_save_u8(io, &command->impulse) || !qa_source_save_u8(io, &command->light_level) ||
        !qa_source_save_u8(io, &command->weapon)) return false;
    command->kind = (qa_ruleset_id)kind;
    return command_valid(command) || application_fail(io->error, QA_ERROR_FORMAT, "Saved input command contains invalid values");
}

static bool domain_fields(qa_source_save_io *io, control_command_domain *domain, uint64_t *time_ns)
{
    uint8_t value = (uint8_t)*domain;
    if (!qa_source_save_u8(io, &value) || value > CONTROL_COMMAND_UNIFIED ||
        !qa_source_save_u64(io, time_ns)) return false;
    *domain = (control_command_domain)value;
    return true;
}

static bool domain_owner(qa_application *app, application_provider *source, qa_actor_id actor,
    control_command_domain domain, uint64_t time_ns, bool reading)
{
    if (!source) return false;
    if (domain == CONTROL_COMMAND_SELECTED || domain == CONTROL_COMMAND_UNIFIED) return time_ns == 0;
    if (source != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !saved_source_client(source, actor, NULL)) return false;
    qa_clock_state clock;
    if (!reading) {
        if (!qa_session_clock(app->session, source->owner, &clock)) return false;
        uint64_t pending = domain == CONTROL_COMMAND_QW_SOURCE ? clock.debt_ns : 0;
        if (clock.frame.time_ns > UINT64_MAX - pending || time_ns > clock.frame.time_ns + pending) return false;
    }
    if (domain == CONTROL_COMMAND_Q3_SOURCE)
        return source->component.clock.kind == QA_RULESET_Q3 &&
            (source->kind == APPLICATION_PROVIDER_Q3 || original_q3(source));
    if (domain == CONTROL_COMMAND_Q2_SOURCE)
        return (source->kind == APPLICATION_PROVIDER_Q2 ||
            native_q2_raw_source_client(source, actor)) &&
            (source->component.clock.kind == QA_RULESET_Q2_CLASSIC || source->component.clock.kind == QA_RULESET_Q2_RERELEASE);
    if (domain == CONTROL_COMMAND_NQ_SOURCE)
        return source_client(source) && source->component.clock.kind == QA_RULESET_NETQUAKE &&
            nq_selected_sources_ready(app, actor);
    return source_client(source) && source->component.clock.kind == QA_RULESET_QUAKEWORLD;
}

static bool domain_command(control_command_domain domain, const qa_movement_command *command,
    const application_control_record *control)
{
    if (domain == CONTROL_COMMAND_Q3_SOURCE) return q3_raw_valid(command);
    if (domain == CONTROL_COMMAND_Q2_SOURCE) return q2_raw_valid(command);
    if (domain == CONTROL_COMMAND_NQ_SOURCE) return nq_raw_valid(command);
    if (domain == CONTROL_COMMAND_QW_SOURCE)
        return command_valid(command) && command->kind == QA_RULESET_QUAKEWORLD && command->milliseconds <= 255;
    return command->kind == control->player.state.kind &&
        (command->kind != QA_RULESET_QUAKEWORLD || command->milliseconds <= 255);
}

static bool unified_fields(qa_source_save_io *io, control_unified *receipt)
{
    uint32_t kind = receipt->movement.kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_RULESET_Q3 ||
        !qa_source_save_u64(io, &receipt->sequence) ||
        !qa_source_save_bool(io, &receipt->has_arsenal) ||
        !qa_source_save_string(io, &receipt->arsenal) || !qa_source_save_string(io, &receipt->weapon) ||
        !qa_source_save_bool(io, &receipt->use_holdable) || !qa_source_save_bool(io, &receipt->has_impulse) ||
        !qa_source_save_u8(io, &receipt->impulse)) return false;
    receipt->movement.kind = (qa_ruleset_id)kind;
    double *fields[10]; size_t count = unified_numbers(&receipt->movement, fields);
    for (size_t i = 0; i < count; ++i) if (!qa_source_save_f64(io, fields[i])) return false;
    return unified_valid(receipt) || application_fail(io->error, QA_ERROR_FORMAT, "Saved unified receipt is invalid");
}

static bool unified_owner(qa_application *app, qa_actor_id actor, const control_unified *receipt,
    const qa_movement_command *marker, const application_control_record *control)
{
    qa_movement_command expected = {.kind = receipt->movement.kind, .sequence = receipt->sequence};
    if (!unified_valid(receipt) || receipt->movement.kind != control->player.state.kind ||
        !command_equal(marker, &expected) ||
        marker->sequence != receipt->sequence || marker->kind != receipt->movement.kind) return false;
    if (!receipt->has_arsenal) return true;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    return arsenal && arsenal->owner == receipt->arsenal && (!receipt->weapon ||
        weapon_owner(app, actor, receipt->arsenal, receipt->weapon));
}

bool application_control_frames_fields(qa_source_save_io *io, qa_application *app,
                                        const application_control_record *controls,
                                        struct application_control_frames **decoded, qa_error *error)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    struct application_control_frames *owner = app->control_frames;
    if (!reading && !application_control_frames_idle(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Input continuation cannot interrupt a source command");
    if (reading) {
        owner = calloc(1, sizeof(*owner));
        if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating saved source input owner");
        owner->capacity = app->control_capacity; owner->application = app;
        owner->inputs = calloc(owner->capacity, sizeof(*owner->inputs));
        if (!owner->inputs) { free(owner); return application_fail(error, QA_ERROR_MEMORY, "Allocating saved retained inputs"); }
    }
    size_t count = 0;
    if (!reading && owner) for (const control_input_slot *slot = owner->first_input; slot; slot = slot->next)
        if (slot->value.actor.registry) ++count;
    bool ok = qa_source_save_count(io, &count, app->control_capacity);
    uint32_t previous = 0;
    const control_input_slot *saved = owner ? owner->first_input : NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        control_input value = {0};
        if (!reading) {
            while (!saved->value.actor.registry) saved = saved->next;
            value = saved->value;
        }
        ok = qa_source_save_actor(io, &value.actor) && qa_source_save_string(io, &value.provider) &&
            qa_source_save_bool(io, &value.seen) && qa_source_save_bool(io, &value.retained) &&
            qa_source_save_u64(io, &value.sequence) && qa_source_save_u8(io, &value.impulse) &&
            qa_source_save_string(io, &value.arsenal) && qa_source_save_string(io, &value.weapon) &&
            domain_fields(io, &value.domain, &value.source_time_ns) &&
            qa_source_save_u64(io, &value.accepted_time_ns) &&
            qa_source_save_u64(io, &value.qw_receipt_time_ns) &&
            qa_source_save_bool(io, &value.has_body_base);
        if (ok && value.has_body_base) ok = qa_source_save_vec3(io, &value.body_base.mins) &&
            qa_source_save_vec3(io, &value.body_base.maxs);
        if (ok && value.seen) ok = command_fields(io, &value.latest);
        if (ok && value.domain == CONTROL_COMMAND_UNIFIED) ok = unified_fields(io, &value.unified);
        if (!ok) break;
        application_provider *source = saved_source_provider(app, value.actor, error);
        qa_clock_state accepted_clock;
        bool accepted_clock_valid = reading || (source &&
            qa_session_clock(app->session, source->owner, &accepted_clock) &&
            value.accepted_time_ns <= accepted_clock.frame.time_ns);
        if (value.actor.slot >= app->control_capacity || !controls[value.actor.slot].active ||
            !qa_actor_id_equal(controls[value.actor.slot].player.actor, value.actor) ||
            !source || source->owner != value.provider || !domain_owner(app, source, value.actor, value.domain, value.source_time_ns, reading) ||
            !accepted_clock_valid || (!value.seen && value.accepted_time_ns) ||
            (value.domain != CONTROL_COMMAND_SELECTED && value.domain != CONTROL_COMMAND_UNIFIED &&
             value.accepted_time_ns != value.source_time_ns) ||
            (value.domain == CONTROL_COMMAND_QW_SOURCE ? value.qw_receipt_time_ns > value.source_time_ns :
                value.qw_receipt_time_ns != 0) ||
            (reading && i && value.actor.slot <= previous) ||
            (value.seen && controls[value.actor.slot].command_seen && value.sequence < controls[value.actor.slot].player.command_sequence) ||
            (!value.seen && (value.sequence || value.impulse)) || (!value.retained && value.impulse) ||
            (!value.retained && ((!value.seen && !value.has_body_base) ||
                (value.domain != CONTROL_COMMAND_Q3_SOURCE && (value.arsenal || value.weapon)))) ||
            (value.has_body_base && (!body_base_owner(source, controls[value.actor.slot].player.state.kind) ||
                !qa_vec_finite(value.body_base.mins) || !qa_vec_finite(value.body_base.maxs) ||
                value.body_base.mins.x > value.body_base.maxs.x || value.body_base.mins.y > value.body_base.maxs.y ||
                value.body_base.mins.z > value.body_base.maxs.z)) ||
            ((value.arsenal || value.weapon) &&
                (!application_player_bot(app, value.actor) || !application_bots_actor(app, value.actor))) ||
            !weapon_owner(app, value.actor, value.arsenal, value.weapon) ||
            (value.seen && (!domain_command(value.domain, &value.latest, &controls[value.actor.slot]) ||
                (value.domain == CONTROL_COMMAND_UNIFIED && !unified_owner(app, value.actor, &value.unified,
                    &value.latest, &controls[value.actor.slot])) ||
                value.latest.sequence != value.sequence ||
                (value.domain == CONTROL_COMMAND_Q2_SOURCE && value.latest.kind !=
                    (source->component.clock.kind == QA_RULESET_Q2_RERELEASE ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC)))) ||
            (!value.seen && value.domain != CONTROL_COMMAND_SELECTED) ||
            (value.retained && value.seen && value.latest.impulse != 0)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved retained input has no exact control owner"); break;
        }
        bool retained = source_client(source) && source->component.clock.kind == QA_RULESET_NETQUAKE &&
            (controls[value.actor.slot].player.state.kind == QA_RULESET_NETQUAKE || value.domain == CONTROL_COMMAND_NQ_SOURCE);
        if (value.retained != retained) { ok = application_fail(error, QA_ERROR_FORMAT, "Saved input retention differs from source ownership"); break; }
        if (reading) { previous = value.actor.slot; *input_enroll(owner, value.actor.slot) = value; }
        else saved = saved->next;
    }
    count = 0;
    if (!reading && owner) for (control_group *group = owner->head; group; group = group->next) ++count;
    if (ok) ok = qa_source_save_count(io, &count, SIZE_MAX / sizeof(control_group));
    control_group *source = !reading && owner ? owner->head : NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        control_group value = {0};
        if (!reading) value = *source;
        ok = qa_source_save_actor(io, &value.actor) && qa_source_save_string(io, &value.provider) &&
            qa_source_save_string(io, &value.arsenal) && qa_source_save_string(io, &value.weapon) &&
            qa_source_save_bool(io, &value.quakeworld) &&
            qa_source_save_bool(io, &value.before_source) &&
            qa_source_save_bool(io, &value.bot) &&
            domain_fields(io, &value.domain, &value.source_time_ns) &&
            qa_source_save_count(io, &value.count, (SIZE_MAX - sizeof(control_group)) / sizeof(qa_movement_command)) && value.count != 0;
        if (ok && value.domain == CONTROL_COMMAND_UNIFIED) ok = unified_fields(io, &value.unified);
        control_group *group = reading && ok ? calloc(1, sizeof(*group) + value.count * sizeof(*group->commands)) : source;
        if (ok && !group) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating saved input group");
        if (reading && ok) { *group = value; if (owner->tail) owner->tail->next = group; else owner->head = group; owner->tail = group; }
        application_provider *actor_source = ok ? saved_source_provider(app, value.actor, error) : NULL;
        if (ok && (value.actor.slot >= app->control_capacity || !controls[value.actor.slot].active ||
            !qa_actor_id_equal(controls[value.actor.slot].player.actor, value.actor) ||
            !actor_source || actor_source->owner != value.provider ||
            value.domain == CONTROL_COMMAND_NQ_SOURCE ||
            !domain_owner(app, actor_source, value.actor, value.domain, value.source_time_ns, reading) ||
            !qa_actor_id_equal(owner->inputs[value.actor.slot].value.actor, value.actor) ||
            owner->inputs[value.actor.slot].value.retained || (!value.bot && (value.arsenal || value.weapon)) ||
            !weapon_owner(app, value.actor, value.arsenal, value.weapon)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved input group has no exact control owner");
        bool qw = value.domain != CONTROL_COMMAND_UNIFIED && actor_source && source_client(actor_source) && actor_source->component.clock.kind == QA_RULESET_QUAKEWORLD &&
            (value.domain == CONTROL_COMMAND_QW_SOURCE || controls[value.actor.slot].player.state.kind == QA_RULESET_QUAKEWORLD);
        if (ok && (qw != value.quakeworld || before_source(actor_source) != value.before_source ||
            (qw && value.count > 20) || (value.domain == CONTROL_COMMAND_Q3_SOURCE &&
                (value.count != 1 || actor_source->kind != APPLICATION_PROVIDER_Q3)) ||
            (value.domain == CONTROL_COMMAND_Q2_SOURCE && (value.count != 1 ||
                (actor_source->kind != APPLICATION_PROVIDER_Q2 &&
                 !application_native_q2_source_client(actor_source, value.actor)))) ||
            (value.domain == CONTROL_COMMAND_UNIFIED && (value.count != 1 || value.bot || value.quakeworld)) ||
            (value.bot && (value.count != 1 ||
                !application_player_bot(app, value.actor) || !application_bots_actor(app, value.actor)))))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved command group differs from source ownership");
        uint64_t last = 0;
        bool seen = ok && controls[value.actor.slot].command_seen;
        if (seen) last = controls[value.actor.slot].player.command_sequence;
        if (ok) for (control_group *prior = owner->head; prior && prior != group; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, value.actor) && prior->count) {
                seen = true; last = prior->commands[prior->count - 1].sequence;
            }
        for (size_t j = 0; ok && j < value.count; ++j) {
            qa_movement_command command = reading ? (qa_movement_command){0} : group->commands[j];
            ok = command_fields(io, &command) && domain_command(value.domain, &command, &controls[value.actor.slot]) &&
                (value.domain != CONTROL_COMMAND_UNIFIED || unified_owner(app, value.actor, &value.unified,
                    &command, &controls[value.actor.slot])) &&
                (value.domain != CONTROL_COMMAND_Q2_SOURCE || command.kind ==
                    (actor_source->component.clock.kind == QA_RULESET_Q2_RERELEASE ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC)) &&
                (j && value.quakeworld ? command.sequence == last : !seen || command.sequence > last) &&
                command.sequence <= owner->inputs[value.actor.slot].value.sequence;
            if (ok) { seen = true; last = command.sequence; if (reading) group->commands[j] = command; }
        }
        if (!reading && source) source = source->next;
    }
    for (const control_input_slot *slot = owner ? owner->first_input : NULL; ok && slot; slot = slot->next) {
        const control_input *input = &slot->value;
        if (!input->actor.registry || input->retained) continue;
        const control_group *tail = NULL;
        for (const control_group *group = owner->head; group; group = group->next)
            if (qa_actor_id_equal(group->actor, input->actor)) tail = group;
        if (tail && (tail->commands[tail->count - 1].sequence != input->sequence ||
            tail->domain != input->domain || tail->source_time_ns != input->source_time_ns ||
            !command_equal(&tail->commands[tail->count - 1], &input->latest) ||
            (input->domain == CONTROL_COMMAND_UNIFIED && !unified_equal(&tail->unified, &input->unified))))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved input queue omits its actual latest received command");
    }
    if (!ok && reading) application_control_frames_free(owner);
    else if (ok && reading) *decoded = owner;
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid saved source input continuation");
    return ok;
}
