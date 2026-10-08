#include "actions_private.h"
#include "qa/builtin.h"
#include "checkpoint_internal.h"
#include "qa/binary.h"
#include "qa/text.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This source bit deliberately aliases CROUCH in the public input record. */
#define BOT_JUMPED_LAST_FRAME UINT32_C(0x80)
#define BOT_COMMAND_CAPACITY 32000

static bool action_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static uint8_t *action_read(const qa_bot_actions *actions, uint32_t client, qa_error *e) {
    if (!actions || !actions->initialized || client >= actions->capacity) {
        action_fail(e, "bot action client is outside the initialized instance");
        return NULL;
    }
    qa_bot_memory_span span;
    if(!qa_bot_memory_bytes(actions->memory,actions->inputs,&span,e)) return NULL;
    if(actions->capacity>INT32_MAX/40 || span.size!=actions->capacity*40) {
        action_fail(e,"bot action source allocation has an invalid extent");return NULL;
    }
    return span.data+client*40;
}
static uint8_t *action_input(qa_bot_actions *actions, uint32_t client, qa_error *e) {
    if (actions && actions->restoring) {
        action_fail(e,"bot actions are preparing a checkpoint");return NULL;
    }
    return action_read(actions,client,e);
}
static float load_float(const uint8_t *p) {
    uint32_t word=qa_load_u32le(p);float value;memcpy(&value,&word,4);return value;
}
static void store_float(uint8_t *p,float value) {
    uint32_t word;memcpy(&word,&value,4);qa_store_u32le(p,word);
}
static qa_vec3 load_vector(const uint8_t *p) {
    return (qa_vec3){load_float(p),load_float(p+4),load_float(p+8)};
}
static void store_vector(uint8_t *p,qa_vec3 value) {
    store_float(p,value.x);store_float(p+4,value.y);store_float(p+8,value.z);
}
static void input_read(const uint8_t *p,qa_bot_input *out) {
    out->think_time=load_float(p);out->direction=load_vector(p+4);
    out->speed=load_float(p+16);out->view_angles=load_vector(p+20);
    out->action_flags=qa_load_u32le(p+32);
    uint32_t weapon=qa_load_u32le(p+36);memcpy(&out->weapon,&weapon,4);
}
bool qa_bot_actions_create_source(qa_bot_memory *memory,const qa_bot_action_services *services,
                           qa_bot_actions **out, qa_error *e) {
    if (!services || !out)
        return action_fail(e, "invalid bot action services or output");
    qa_bot_actions *actions = calloc(1, sizeof(*actions));
    if (!actions) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot actions");
        return false;
    }
    actions->services = *services;
    if(memory) {
        if(!qa_bot_memory_retain(memory,e)) {free(actions);return false;}
    } else {
        if(!qa_bot_memory_create(NULL,&memory,e)) {free(actions);return false;}
        actions->owns_memory=true;
    }
    actions->memory=memory;
    *out = actions;
    return true;
}
bool qa_bot_actions_create(uint32_t clients,const qa_bot_action_services *services,
                           qa_bot_actions **out,qa_error *e) {
    if(!out) return action_fail(e,"missing bot actions output");
    qa_bot_actions *actions;
    if(!qa_bot_actions_create_source(NULL,services,&actions,e)) return false;
    if(!qa_bot_actions_setup(actions,clients,e)) {qa_bot_actions_destroy(actions);return false;}
    *out=actions;return true;
}
void qa_bot_actions_destroy(qa_bot_actions *actions) {
    if (actions && qa_bot_actions_idle(actions) && qa_bot_memory_idle(actions->memory)) {
        actions->busy=true;
        (void)qa_bot_memory_release(actions->memory,NULL);
        free(actions);
    }
}
uint32_t qa_bot_actions_capacity(const qa_bot_actions *actions) {
    return actions ? actions->capacity : 0;
}
bool qa_bot_actions_idle(const qa_bot_actions *actions) {
    return !actions || (!actions->busy && !actions->restoring && !actions->operations);
}
qa_bot_memory *qa_bot_actions_memory(const qa_bot_actions *actions) {
    return actions?actions->memory:NULL;
}
bool qa_bot_actions_setup(qa_bot_actions *actions, uint32_t clients, qa_error *e) {
    if (!actions || actions->restoring || actions->busy || clients > INT32_MAX / 40)
        return action_fail(e, "bot action capacity exceeds source allocation range");
    qa_bot_memory_allocation inputs;
    actions->busy=true;
    bool allocated=qa_bot_memory_allocate(actions->memory,clients*40,QA_BOT_MEMORY_HUNK,true,
        NULL,&inputs,e);
    actions->busy=false;
    if(!allocated) return false;
    actions->inputs = inputs;
    actions->capacity = clients;
    actions->initialized = true;
    return true;
}
bool qa_bot_actions_shutdown(qa_bot_actions *actions,qa_error *e) {
    if (!actions || actions->restoring || actions->busy) return action_fail(e,"bot action owner is absent or restoring");
    actions->busy=true;
    bool freed=!actions->initialized || qa_bot_memory_free(actions->memory,actions->inputs,e);
    actions->busy=false;
    if(!freed) return false;
    actions->inputs = (qa_bot_memory_allocation){0};
    actions->initialized = false;
    return true;
}
void qa_bot_actions_dispose_resources(qa_bot_actions *actions) {
    if(actions && !actions->restoring && !actions->busy) {
        actions->inputs=(qa_bot_memory_allocation){0};actions->initialized=false;
    }
}
bool qa_bot_actions_add(qa_bot_actions *actions, uint32_t client, uint32_t flags, qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    qa_store_u32le(input+32,qa_load_u32le(input+32)|flags);
    return true;
}
bool qa_bot_actions_weapon(qa_bot_actions *actions, uint32_t client, int32_t weapon, qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    qa_store_u32le(input+36,(uint32_t)weapon);
    return true;
}
bool qa_bot_actions_jump(qa_bot_actions *actions, uint32_t client, bool delayed, qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    uint32_t flag = delayed ? QA_BOT_DELAYED_JUMP : QA_BOT_JUMP;
    uint32_t flags=qa_load_u32le(input+32);
    qa_store_u32le(input+32,flags&BOT_JUMPED_LAST_FRAME?flags&~flag:flags|flag);
    return true;
}
bool qa_bot_actions_move(qa_bot_actions *actions, uint32_t client, qa_vec3 direction, float speed,
                         qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    store_vector(input+4,direction);
    store_float(input+16,speed > 400 ? 400 : speed < -400 ? -400 : speed);
    return true;
}
bool qa_bot_actions_view(qa_bot_actions *actions, uint32_t client, qa_vec3 angles, qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    store_vector(input+20,angles);
    return true;
}
static bool vector_from(qa_bot_actions *actions,uint32_t client,qa_bot_action_vector_read read,
    void *context,bool moving,float speed,qa_error *e) {
    if(!read) return action_fail(e,"missing lazy source action vector reader");
    if(!action_input(actions,client,e)) return false;
    qa_bot_memory_allocation allocation=actions->inputs;
    qa_vec3 value;
    if(actions->operations==SIZE_MAX) return action_fail(e,"source action callback nesting exceeds capacity");
    ++actions->operations;
    bool ok=read(context,&value,e);
    --actions->operations;
    if(!ok) return false;
    qa_bot_memory_span span;
    if(!qa_bot_memory_bytes(actions->memory,allocation,&span,e)) return false;
    if(span.size/40<=client) return action_fail(e,"lazy source action allocation has changed extent");
    uint8_t *input=span.data+client*40;
    store_vector(input+(moving?4:20),value);
    if(moving) store_float(input+16,speed>400?400:speed< -400? -400:speed);
    return true;
}
bool qa_bot_actions_move_from(qa_bot_actions *actions,uint32_t client,qa_bot_action_vector_read read,
    void *context,float speed,qa_error *e) {
    return vector_from(actions,client,read,context,true,speed,e);
}
bool qa_bot_actions_view_from(qa_bot_actions *actions,uint32_t client,qa_bot_action_vector_read read,
    void *context,qa_error *e) {
    return vector_from(actions,client,read,context,false,0,e);
}
bool qa_bot_actions_input(qa_bot_actions *actions, uint32_t client, float think_time,
                          qa_bot_input *out, qa_error *e) {
    if (!out)
        return action_fail(e, "missing bot input output");
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    store_float(input,think_time);
    input_read(input,out);
    return true;
}
bool qa_bot_actions_input_bytes(qa_bot_actions *actions,uint32_t client,float think_time,
                                qa_bytes *out,qa_error *e) {
    if(!out) return action_fail(e,"missing source bot input byte output");
    uint8_t *input=action_input(actions,client,e);
    if(!input) return false;
    store_float(input,think_time);*out=(qa_bytes){input,40};return true;
}
bool qa_bot_actions_read(const qa_bot_actions *actions, uint32_t client, qa_bot_input *out,
                         qa_error *e) {
    if (!out)
        return action_fail(e, "missing bot input output");
    const uint8_t *input = action_read(actions, client, e);
    if (!input)
        return false;
    input_read(input,out);
    return true;
}
bool qa_bot_actions_restore(qa_bot_actions *actions, uint32_t client, const qa_bot_input *saved,
                            qa_error *e) {
    if (!saved)
        return action_fail(e, "missing saved bot input");
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    store_float(input,saved->think_time);store_vector(input+4,saved->direction);
    store_float(input+16,saved->speed);store_vector(input+20,saved->view_angles);
    qa_store_u32le(input+32,saved->action_flags);qa_store_u32le(input+36,(uint32_t)saved->weapon);
    return true;
}
void bot_action_restore_lock(qa_bot_actions *actions, bool locked) { actions->restoring=locked; }
bool bot_action_snapshot_capture(qa_bot_actions *actions,bot_action_snapshot *out,qa_error *e) {
    if(!actions || !out || actions->busy || actions->operations || actions->capacity>INT32_MAX/40 ||
       actions->initialized!=(actions->inputs.owner!=0))
        return action_fail(e,"bot action source alias is inconsistent");
    if(actions->initialized) {
        qa_bot_memory_span span;qa_bot_memory_kind kind;
        if(!qa_bot_memory_bytes(actions->memory,actions->inputs,&span,e) ||
           !qa_bot_memory_kind_read(actions->memory,actions->inputs,&kind,e)) return false;
        if(span.size!=actions->capacity*40 || kind!=QA_BOT_MEMORY_HUNK)
            return action_fail(e,"bot action source extent or kind differs from allocation");
    }
    *out=(bot_action_snapshot){actions->inputs,actions->capacity,actions->initialized};return true;
}
bool bot_action_snapshot_prepare(qa_bot_actions *actions,const bot_action_snapshot *saved,
    const qa_bot_memory_prepared *memory,bot_action_snapshot *out,qa_error *e) {
    if(!actions || !saved || !out || !actions->restoring || saved->capacity>INT32_MAX/40 ||
       saved->initialized!=(saved->inputs.owner!=0))
        return action_fail(e,"invalid prepared bot action alias");
    *out=*saved;
    return !saved->initialized || qa_bot_memory_checkpoint_resolve(memory,saved->inputs,&out->inputs,e);
}
void bot_action_snapshot_finish(qa_bot_actions *actions,const bot_action_snapshot *prepared,bool commit) {
    if(commit) {actions->inputs=prepared->inputs;actions->capacity=prepared->capacity;
        actions->initialized=prepared->initialized;}
}
bool qa_bot_actions_reset(qa_bot_actions *actions, uint32_t client, qa_error *e) {
    uint8_t *input = action_input(actions, client, e);
    if (!input)
        return false;
    qa_store_u32le(input+32,qa_load_u32le(input+32)&~BOT_JUMPED_LAST_FRAME);
    store_float(input,0);store_vector(input+4,(qa_vec3){0});store_float(input+16,0);
    bool jumped=(qa_load_u32le(input+32)&QA_BOT_JUMP)!=0;
    qa_store_u32le(input+32,jumped?BOT_JUMPED_LAST_FRAME:0);
    return true;
}
void qa_bot_actions_end_regular(qa_bot_actions *actions, int32_t client, float think_time) {
    (void)actions;
    (void)client;
    (void)think_time;
}
bool qa_bot_actions_text(qa_bot_actions *actions, int32_t client, qa_bot_text_action action,
                         int32_t recipient, const char *text, qa_error *e) {
    if (!actions || actions->restoring || !actions->services.command || !text || action < QA_BOT_COMMAND ||
        action > QA_BOT_DROP_INVENTORY)
        return action_fail(e, "invalid bot command action");
    if (action == QA_BOT_COMMAND) {
        if(actions->operations==SIZE_MAX) return action_fail(e,"source action callback nesting exceeds capacity");
        ++actions->operations;
        bool ok=actions->services.command(actions->services.context,client,text,e);
        --actions->operations;return ok;
    }
    const char *prefix = "";
    char target[32];
    switch (action) {
    case QA_BOT_SAY: prefix = "say "; break;
    case QA_BOT_SAY_TEAM: prefix = "say_team "; break;
    case QA_BOT_TELL:
        snprintf(target, sizeof(target), "tell %d, ", recipient);
        prefix = target;
        break;
    case QA_BOT_USE_ITEM: prefix = "use "; break;
    case QA_BOT_DROP_ITEM: prefix = "drop "; break;
    case QA_BOT_USE_INVENTORY: prefix = "invuse "; break;
    case QA_BOT_DROP_INVENTORY: prefix = "invdrop "; break;
    case QA_BOT_COMMAND: break;
    }
    size_t first = strlen(prefix), length = strlen(text);
    if (length >= BOT_COMMAND_CAPACITY - first)
        return action_fail(e, "bot formatted command exceeds source va buffer");
    char command[BOT_COMMAND_CAPACITY];
    memcpy(command, prefix, first);
    memcpy(command + first, text, length + 1);
    if(actions->operations==SIZE_MAX) return action_fail(e,"source action callback nesting exceeds capacity");
    ++actions->operations;
    bool ok=actions->services.command(actions->services.context,client,command,e);
    --actions->operations;return ok;
}

static int32_t source_integer(float value) {
    return value >= -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN;
}
static float angle_mod(float angle) {
    return (float)((uint32_t)source_integer(angle * (65536.0f / 360.0f)) & 65535u) *
           (360.0f / 65536.0f);
}
float qa_bot_angle_difference(float angle, float ideal) {
    float difference = angle - ideal;
    if (angle > ideal) {
        if (difference > 180)
            difference -= 360;
    } else if (difference < -180)
        difference += 360;
    return difference;
}
bool qa_bot_field_of_vision(qa_vec3 view,float degrees,qa_vec3 target) {
    float from[2]={view.x,view.y},to[2]={target.x,target.y};
    for(uint32_t i=0;i<2;++i) {
        float angle=angle_mod(from[i]),ideal=angle_mod(to[i]);
        float difference=qa_bot_angle_difference(ideal,angle);
        if(difference>degrees*.5f || difference<-degrees*.5f) return false;
    }
    return true;
}
float qa_bot_change_angle(float angle, float ideal, float speed) {
    angle = angle_mod(angle);
    ideal = angle_mod(ideal);
    if (angle == ideal)
        return angle;
    float move = -qa_bot_angle_difference(angle, ideal);
    if (move > speed)
        move = speed;
    else if (move < -speed)
        move = -speed;
    return angle_mod(angle + move);
}
void qa_bot_change_view(qa_bot_view_state *state, float factor, float maximum, float elapsed,
                        bool challenge) {
    if (!state)
        return;
    if (state->ideal.x > 180)
        state->ideal.x -= 360;
    if (maximum < 240)
        maximum = 240;
    maximum *= elapsed;
    float angles[2] = {state->angles.x, state->angles.y};
    float ideals[2] = {state->ideal.x, state->ideal.y};
    float velocity[2] = {state->velocity.x, state->velocity.y};
    for (unsigned i = 0; i < 2; ++i) {
        if (challenge) {
            int32_t integer = source_integer(qa_bot_angle_difference(angles[i], ideals[i]));
            float difference = integer == INT32_MIN ? (float)INT32_MIN : (float)abs(integer);
            float speed = difference * factor;
            angles[i] = qa_bot_change_angle(angles[i], ideals[i], speed > maximum ? maximum : speed);
        } else {
            angles[i] = angle_mod(angles[i]);
            ideals[i] = angle_mod(ideals[i]);
            float desired = qa_bot_angle_difference(angles[i], ideals[i]) * factor;
            velocity[i] += velocity[i] - desired;
            if (velocity[i] > 180)
                velocity[i] = maximum;
            if (velocity[i] < -180)
                velocity[i] = -maximum;
            float speed = velocity[i] > maximum ? maximum :
                          velocity[i] < -maximum ? -maximum : velocity[i];
            angles[i] = angle_mod(angles[i] + speed);
            velocity[i] *= .45f * (1 - factor);
        }
    }
    state->angles.x = angles[0] > 180 ? angles[0] - 360 : angles[0];
    state->angles.y = angles[1];
    state->ideal.x = ideals[0];
    state->ideal.y = ideals[1];
    state->velocity.x = velocity[0];
    state->velocity.y = velocity[1];
}
void qa_bot_view_delta(qa_bot_view_state *state, const int32_t delta[3], bool add) {
    if (!state || !delta)
        return;
    float sign = add ? 1 : -1;
    state->angles.x = angle_mod(state->angles.x + sign * (float)delta[0] * (360.0f / 65536.0f));
    state->angles.y = angle_mod(state->angles.y + sign * (float)delta[1] * (360.0f / 65536.0f));
    state->angles.z = angle_mod(state->angles.z + sign * (float)delta[2] * (360.0f / 65536.0f));
}
void qa_bot_input_intent(const qa_bot_input *input, qa_input_command_intent *out) {
    uint32_t flags = input->action_flags;
    if (flags & QA_BOT_DELAYED_JUMP) flags = (flags | QA_BOT_JUMP) & ~(uint32_t)QA_BOT_DELAYED_JUMP;
    qa_input_command_intent intent = {.angles = input->view_angles, .speed = input->speed,
        .directional = true, .walking = (flags & QA_BOT_WALK) != 0, .game_focus = true};
    static const struct { uint32_t source; qa_input_action action; } actions[] = {
        {QA_BOT_RESPAWN | QA_BOT_ATTACK, QA_INPUT_ATTACK}, {QA_BOT_TALK, QA_INPUT_BUTTON1},
        {QA_BOT_GESTURE, QA_INPUT_BUTTON3}, {QA_BOT_USE, QA_INPUT_USE},
        {QA_BOT_AFFIRMATIVE, QA_INPUT_BUTTON5}, {QA_BOT_NEGATIVE, QA_INPUT_BUTTON6},
        {QA_BOT_GET_FLAG, QA_INPUT_BUTTON7}, {QA_BOT_GUARD_BASE, QA_INPUT_BUTTON8},
        {QA_BOT_PATROL, QA_INPUT_BUTTON9}, {QA_BOT_FOLLOW_ME, QA_INPUT_BUTTON10},
        {QA_BOT_MOVE_FORWARD, QA_INPUT_FORWARD}, {QA_BOT_MOVE_BACK, QA_INPUT_BACK},
        {QA_BOT_MOVE_LEFT, QA_INPUT_MOVE_LEFT}, {QA_BOT_MOVE_RIGHT, QA_INPUT_MOVE_RIGHT},
        {QA_BOT_JUMP, QA_INPUT_JUMP}, {QA_BOT_CROUCH, QA_INPUT_CROUCH}
    };
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); ++i)
        if (flags & actions[i].source) intent.actions |= UINT64_C(1) << actions[i].action;
    qa_vec3 forward, right;
    qa_builtin_angle_vectors(qa_v3(input->direction.z != 0 ? input->view_angles.x : 0,
                                  input->view_angles.y, 0), &forward, &right, NULL);
    int32_t vertical = qa_source_float_to_i32(forward.z);
    float up = vertical == INT32_MIN ? (float)INT32_MIN : (float)abs(vertical);
    intent.direction = qa_v3(qa_vec_dot(forward, input->direction), qa_vec_dot(right, input->direction),
                             up * input->direction.z);
    *out = intent;
}
