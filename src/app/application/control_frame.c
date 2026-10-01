#include "control_frame.h"
#include "guest_input_private.h"
#include "bots_round.h"
#include "native_q3_clients.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_source.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum control_command_domain {
    CONTROL_COMMAND_SELECTED,
    CONTROL_COMMAND_QW_SOURCE,
    CONTROL_COMMAND_Q3_SOURCE
} control_command_domain;

typedef struct control_input {
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_movement_command latest;
    uint64_t sequence;
    control_command_domain domain;
    uint64_t source_time_ns;
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
    qa_movement_command commands[];
} control_group;
struct application_control_frames {
    qa_application *application;
    control_input *inputs;
    uint32_t capacity;
    control_group *head, *tail;
    const application_control_context *current;
    qa_movement_state *state;
    qa_actor_id state_actor;
    const control_group *q3_group;
    qa_actor_id *touched;
    size_t touched_count, touched_capacity;
    bool draining;
};

static bool command_valid(const qa_movement_command *command)
{
    return (unsigned)command->kind <= QA_MOVEMENT_Q3 && qa_vec_finite(command->angles) &&
        isfinite(command->forward_move) && isfinite(command->side_move) &&
        isfinite(command->up_move) && isfinite(command->acknowledged_server_seconds);
}

static int8_t q3_axis(float value)
{
    if (value > 127) return 127;
    if (value < -127) return -127;
    return (int8_t)lrintf(value);
}

static qa_movement_command q3_command(const qa_q3_usercmd *raw, uint64_t sequence)
{
    qa_movement_command command = {.kind = QA_MOVEMENT_Q3, .sequence = sequence,
        .server_time_ms = raw->serverTime, .buttons = (uint32_t)raw->buttons, .weapon = raw->weapon,
        .forward_move = raw->forwardmove, .side_move = raw->rightmove, .up_move = raw->upmove};
    memcpy(command.angle_words, raw->angles, sizeof(command.angle_words));
    return command;
}

static qa_q3_usercmd q3_source_command(const qa_movement_command *command, bool raw)
{
    qa_q3_usercmd out = {.serverTime = command->server_time_ms,
        .buttons = (int32_t)command->buttons, .weapon = command->weapon,
        .forwardmove = raw ? (int8_t)command->forward_move : q3_axis(command->forward_move),
        .rightmove = raw ? (int8_t)command->side_move : q3_axis(command->side_move),
        .upmove = raw ? (int8_t)command->up_move : q3_axis(command->up_move)};
    memcpy(out.angles, command->angle_words, sizeof(out.angles));
    return out;
}

static bool q3_raw_valid(const qa_movement_command *command)
{
    return command_valid(command) && command->kind == QA_MOVEMENT_Q3 &&
        command->milliseconds == 0 && command->server_frame == 0 &&
        command->acknowledged_server_seconds == 0 && command->angles.x == 0 &&
        command->angles.y == 0 && command->angles.z == 0 && !command->impulse && !command->light_level &&
        command->forward_move >= -128 && command->forward_move <= 127 &&
        command->side_move >= -128 && command->side_move <= 127 &&
        command->up_move >= -128 && command->up_move <= 127 &&
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

static bool saved_source_client(application_provider *map, qa_actor_id actor, qa_error *error)
{
    uint32_t slot; bool member = false;
    if (map && map->constructed && map->attached && !map->close_pending) {
        if (map->kind == APPLICATION_PROVIDER_Q1)
            member = qa_q1_native_client_slot_prepared(map->state.q1, actor, &slot, NULL);
        else if (map->kind == APPLICATION_PROVIDER_Q3)
            member = qa_q3_native_client_slot(map->state.q3, actor, &slot, NULL);
        else if (map->kind == APPLICATION_PROVIDER_QVM ||
            (map->kind == APPLICATION_PROVIDER_NATIVE && map->component.clock.kind == QA_CLOCK_Q3))
            member = application_q3_guest_actor_client(map, actor, &slot);
        else if (map->kind == APPLICATION_PROVIDER_QC && !map->state.qc.qualified) {
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
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->component.clock.kind == QA_CLOCK_Q3));
}

static bool body_base_owner(const application_provider *source, qa_movement_kind kind)
{
    return kind == QA_MOVEMENT_NETQUAKE || (kind == QA_MOVEMENT_QUAKEWORLD && source &&
        source->kind == APPLICATION_PROVIDER_QC && !source->state.qc.qualified &&
        source->component.clock.kind == QA_CLOCK_QUAKEWORLD);
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
    if (!record->active || !qa_actor_id_equal(record->actor, actor) || !source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Body request lost its selected movement owner");
    if (!body_base_owner(source, record->state.kind)) return true;
    control_input *input = &app->control_frames->inputs[actor.slot];
    if (!qa_actor_id_equal(input->actor, actor)) {
        if (!outputs->has_body_bounds) return true;
        if (input->actor.registry || input->turn) *input = (control_input){0};
        input->actor = actor; input->provider = source->owner;
        input->retained = source_client(source) && source->component.clock.kind == QA_CLOCK_NETQUAKE &&
            record->state.kind == QA_MOVEMENT_NETQUAKE;
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
    control_input *input = &app->control_frames->inputs[actor.slot];
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

static uint64_t source_interval(qa_movement_kind kind, uint64_t host_elapsed_ns)
{
    if (kind != QA_MOVEMENT_NETQUAKE) return host_elapsed_ns;
    return host_elapsed_ns < UINT64_C(1000000) ? UINT64_C(1000000) :
        host_elapsed_ns > UINT64_C(100000000) ? UINT64_C(100000000) : host_elapsed_ns;
}

static bool weapon_owner(qa_application *app, qa_actor_id actor, qa_actor_owner owner, qa_item_id item)
{
    if (!owner && !item) return true;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    qa_actor_owner actual;
    return owner && item && arsenal && arsenal->owner == owner &&
        qa_inventory_item_owner(app->inventory, actor, item, &actual, NULL) && actual == owner;
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
    for (uint32_t i = 0; i < frames->capacity; ++i)
        (void)application_control_turn_abort(frames->inputs[i].turn, NULL);
    free(frames->touched); free(frames->inputs); free(frames);
}

bool application_control_frames_idle(const qa_application *app)
{
    const struct application_control_frames *frames = app ? app->control_frames : NULL;
    if (!frames) return true;
    if (frames->current || frames->draining || frames->state || frames->q3_group) return false;
    for (uint32_t i = 0; i < frames->capacity; ++i)
        if (frames->inputs[i].turn || frames->inputs[i].source_turn_actor.registry) return false;
    return true;
}

bool application_control_frames_abort(qa_application *app, qa_error *error)
{
    if (!app || !app->control_frames) return true;
    struct application_control_frames *owner = app->control_frames;
    if (owner->current || owner->draining || owner->state)
        return application_fail(error, QA_ERROR_ARGUMENT, "Input cleanup cannot interrupt a current source call");
    bool ok = true; qa_error first = {0};
    for (uint32_t i = 0; i < owner->capacity; ++i) {
        struct application_control_turn *turn = owner->inputs[i].turn;
        owner->inputs[i].turn = NULL; qa_error current = {0};
        owner->inputs[i].source_turn_actor = (qa_actor_id){0};
        owner->inputs[i].source_turn_provider = 0;
        owner->inputs[i].source_turn_frame_number = 0;
        if (!application_control_turn_abort(turn, &current)) { if (ok) first = current; ok = false; }
    }
    owner->touched_count = 0;
    if (!ok && error) *error = first;
    return ok;
}

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
    control_input *input = &frames->inputs[actor.slot];
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
    if (!record->active || !qa_actor_id_equal(record->actor, actor)) return false;
    *seen = record->command_seen; *sequence = record->command_sequence;
    const struct application_control_frames *frames = app->control_frames;
    if (frames && qa_actor_id_equal(frames->inputs[actor.slot].actor, actor) && frames->inputs[actor.slot].seen) {
        *seen = true; *sequence = frames->inputs[actor.slot].sequence;
    }
    return true;
}

bool application_control_frames_q1_prepared(const qa_application *app, qa_actor_id actor,
                                             qa_actor_owner provider)
{
    qa_source_frame actual;
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity ||
        !qa_session_active_frame(app->session, provider, &actual)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot];
    return qa_actor_id_equal(input->q1_source_actor, actor) && input->q1_source_provider == provider &&
        input->q1_source_frame_number == actual.number && !input->q1_source_deferred;
}

bool application_control_frames_q1_command_ready(const qa_application *app, qa_actor_id actor,
    qa_actor_owner provider, const qa_movement_command *command)
{
    qa_source_frame actual;
    if (!app || !app->control_frames || actor.slot >= app->control_frames->capacity ||
        !qa_session_active_frame(app->session, provider, &actual)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot];
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
    control_input *input = &app->control_frames->inputs[actor.slot];
    input->q1_source_actor = actor; input->q1_source_provider = provider;
    input->q1_source_frame_number = actual.number;
    input->q1_source_deferred = false; input->q1_source_sequence = 0;
    return true;
}

bool qa_application_control_q3_command(qa_application *app, qa_actor_id actor,
    uint64_t sequence, const qa_q3_usercmd *raw, qa_error *error)
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
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->actor, actor) ||
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
    control_input *input = &frames->inputs[actor.slot];
    if (input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw Q3 intake cannot interrupt a retained input turn");
    control_group *group = deferred ? NULL : malloc(sizeof(*group) + sizeof(qa_movement_command));
    if (!deferred && !group)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating raw Q3 source command");
    qa_movement_command command = q3_command(raw, sequence);
    if (group) {
        *group = (control_group){.actor = actor, .provider = provider->owner, .count = 1,
            .before_source = before_source(provider), .domain = CONTROL_COMMAND_Q3_SOURCE,
            .source_time_ns = clock.frame.time_ns};
        group->commands[0] = command;
    }
    if (provider->kind == APPLICATION_PROVIDER_Q3 &&
        (!application_native_q3_wire_command(provider, slot, raw, error) ||
         !qa_q3_client_received_command(provider->state.q3, actor, raw, error))) {
        free(group); application_fault(app, error); return false;
    }
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true;
    input->retained = false; input->domain = CONTROL_COMMAND_Q3_SOURCE;
    input->source_time_ns = clock.frame.time_ns; input->qw_receipt_time_ns = 0; input->latest = command;
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
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->actor, actor) ||
        !source_client(provider) || provider->component.clock.kind != QA_CLOCK_QUAKEWORLD ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !provider->component.command_actor ||
        !provider->component.command_actor(provider->component.state, app->session, actor) ||
        frames->draining || frames->current || !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW group has no actual physical source client");
    bool seen; uint64_t previous;
    (void)application_control_frames_sequence(app, actor, &seen, &previous);
    for (size_t i = 0; i < count; ++i)
        if (!command_valid(&commands[i]) || commands[i].kind != QA_MOVEMENT_QUAKEWORLD ||
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
    if (receipt_time > clock.frame.time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source receipt clock exceeds its real admission");
    control_input *input = &frames->inputs[actor.slot];
    if (input->turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Raw QW intake cannot interrupt a retained turn");
    control_group *group = malloc(sizeof(*group) + count * sizeof(*commands));
    if (!group) return application_fail(error, QA_ERROR_MEMORY, "Allocating raw QW source group");
    *group = (control_group){.actor = actor, .provider = provider->owner, .count = count,
        .quakeworld = true, .before_source = before_source(provider), .domain = CONTROL_COMMAND_QW_SOURCE,
        .source_time_ns = clock.frame.time_ns};
    memcpy(group->commands, commands, count * sizeof(*commands));
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = commands[0].sequence; input->seen = true;
    input->retained = false; input->domain = CONTROL_COMMAND_QW_SOURCE; input->source_time_ns = clock.frame.time_ns;
    input->qw_receipt_time_ns = receipt_time;
    input->latest = commands[count - 1]; input->arsenal = 0; input->weapon = 0; input->impulse = 0;
    if (frames->tail) frames->tail->next = group; else frames->head = group;
    frames->tail = group;
    return true;
}

bool application_control_last_qw_command(const qa_application *app, qa_actor_id actor,
    qa_movement_command *out, uint64_t *time_ns, bool *present, qa_error *error)
{
    if (!app || !out || !time_ns || !present || !app->control_frames || actor.slot >= app->control_capacity ||
        !app->controls[actor.slot].active || !qa_actor_id_equal(app->controls[actor.slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source input read needs a current full control actor");
    *out = (qa_movement_command){0}; *time_ns = 0; *present = false;
    const control_input *input = &app->control_frames->inputs[actor.slot];
    if (!qa_actor_id_equal(input->actor, actor) || !input->seen || input->domain != CONTROL_COMMAND_QW_SOURCE ||
        input->latest.kind != QA_MOVEMENT_QUAKEWORLD)
        return true;
    *out = input->latest; *time_ns = input->qw_receipt_time_ns; *present = true;
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
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor) || !provider)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Input group has no current execution owner");
    if ((provider->kind == APPLICATION_PROVIDER_Q3 || original_q3(provider)) &&
        record->state.kind == QA_MOVEMENT_Q3) {
        for (size_t i = 0; i < count; ++i)
            if (!command_valid(&commands[i]) || commands[i].kind != QA_MOVEMENT_Q3 ||
                (i && commands[i].sequence <= commands[i - 1].sequence))
                return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 input group has invalid commands");
        for (size_t i = 0; i < count; ++i) {
            qa_q3_usercmd raw = q3_source_command(&commands[i], false);
            if (!qa_application_control_q3_command(app, actor, commands[i].sequence, &raw, error)) return false;
        }
        return true;
    }
    if (record->state.kind == QA_MOVEMENT_QUAKEWORLD && source_client(provider) &&
        provider->component.clock.kind == QA_CLOCK_QUAKEWORLD && provider->component.command_actor &&
        provider->component.command_actor(provider->component.state, app->session, actor))
        return qa_application_control_qw_commands(app, actor, commands, count, error);
    if (frames->draining || (frames->current && qa_actor_id_equal(frames->current->actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Input intake cannot replace an applied command");
    bool seen; uint64_t sequence;
    (void)application_control_frames_sequence(app, actor, &seen, &sequence);
    bool retained = source_client(provider) && provider->component.clock.kind == QA_CLOCK_NETQUAKE &&
        record->state.kind == QA_MOVEMENT_NETQUAKE;
    bool quakeworld = source_client(provider) && provider->component.clock.kind == QA_CLOCK_QUAKEWORLD &&
        record->state.kind == QA_MOVEMENT_QUAKEWORLD;
    if (quakeworld && count > 20)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW input group exceeds its twenty-command source bound");
    for (size_t i = 0; i < count; ++i) {
        if (!command_valid(&commands[i]) || commands[i].kind != record->state.kind ||
            (commands[i].kind == QA_MOVEMENT_QUAKEWORLD && commands[i].milliseconds > 255) ||
            (i && quakeworld ? commands[i].sequence != sequence : seen && commands[i].sequence <= sequence))
            return application_fail(error, QA_ERROR_ARGUMENT, "Input group has invalid dialect, values or sequence");
        seen = true; sequence = commands[i].sequence;
    }
    if (app->q1_paused) return true;
    control_group *group = NULL;
    if (!retained) {
        if (count > (SIZE_MAX - sizeof(*group)) / sizeof(*commands))
            return application_fail(error, QA_ERROR_MEMORY, "Input group is too large");
        group = malloc(sizeof(*group) + count * sizeof(*commands));
        if (!group) return application_fail(error, QA_ERROR_MEMORY, "Allocating pending input group");
        *group = (control_group){.actor = actor, .provider = provider->owner, .count = count,
            .quakeworld = quakeworld, .before_source = before_source(provider)};
        memcpy(group->commands, commands, count * sizeof(*commands));
    }
    control_input *input = &frames->inputs[actor.slot];
    if (input->turn) { free(group); return application_fail(error, QA_ERROR_ARGUMENT, "Retained input preparation is open"); }
    if (!qa_actor_id_equal(input->actor, actor)) *input = (control_input){.actor = actor};
    input->provider = provider->owner; input->sequence = sequence; input->seen = true; input->retained = retained;
    input->domain = CONTROL_COMMAND_SELECTED; input->source_time_ns = 0; input->qw_receipt_time_ns = 0;
    input->arsenal = 0; input->weapon = 0;
    input->latest = commands[count - 1];
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
    control_input *input = &app->control_frames->inputs[actor.slot];
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

bool application_control_last_q3_command(const qa_application *app, qa_actor_id actor,
                                          qa_q3_usercmd *out, qa_error *error)
{
    if (!app || !out || actor.slot >= app->control_capacity || !app->control_frames ||
        !app->controls[actor.slot].active || !qa_actor_id_equal(app->controls[actor.slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 last command needs its admitted control owner");
    *out = (qa_q3_usercmd){0};
    const control_input *input = &app->control_frames->inputs[actor.slot];
    if (!qa_actor_id_equal(input->actor, actor) || !input->seen) return true;
    const qa_movement_command *command = &input->latest;
    if (command->kind != QA_MOVEMENT_Q3)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 input owner contains a different command dialect");
    *out = q3_source_command(command, input->domain == CONTROL_COMMAND_Q3_SOURCE);
    return true;
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
        if (current->source_usercmd || current->source_guestcmd) {
            application_provider *source = NULL;
            for (size_t i = 0; i < app->provider_count; ++i)
                if (app->providers[i]->owner == owner) { source = app->providers[i]; break; }
            uint32_t slot;
            if (!source || !source->attached || !source->constructed || source->close_pending ||
                (current->source_usercmd && (source->kind != APPLICATION_PROVIDER_Q3 ||
                 !qa_q3_native_client_slot(source->state.q3, actor, &slot, NULL))) ||
                (current->source_guestcmd && (!original_q3(source) || !source->component.command_actor ||
                 !source->component.command_actor(source->component.state, app->session, actor)))) return NULL;
        }
    } else {
        application_provider *source = source_provider((qa_application *)app, actor);
        if (!source || source->owner != owner) return NULL;
        qa_source_frame actual;
        if (!qa_session_active_frame(app->session, owner, &actual) || actual.kind != current->frame.kind ||
            actual.number != current->frame.number || actual.time_ns != current->frame.time_ns ||
            actual.start_ns != current->frame.start_ns || actual.elapsed_ns != current->frame.elapsed_ns) return NULL;
    }
    if (current->path == APPLICATION_CONTROL_MIXED) {
        uint64_t interval = current->source_usercmd ? current->command.elapsed_ns :
            current->command_only ? current->command.host_elapsed_ns : current->frame.elapsed_ns;
        if (current->source_guestcmd && current->source_input_applied) {
            if (!application_guest_input_interval(app, actor, &interval)) return NULL;
        } else if (!current->source_usercmd) (void)qa_session_advance_interval(app->session, &interval);
        if (actor.slot >= app->control_capacity || source_interval(app->controls[actor.slot].state.kind, interval)
            != current->source_elapsed_ns) return NULL;
    }
    return frames->current;
}

bool application_control_frames_q3_move(qa_application *app, const qa_source_command *admission,
    const qa_movement_command *command, bool use_holdable, qa_error *error)
{
    if (!app || !app->control_frames || !admission || !command || admission->kind != QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 movement needs its actual source command");
    struct application_control_frames *owner = app->control_frames;
    application_control_context current = {.actor = admission->actor, .path = APPLICATION_CONTROL_MIXED,
        .stage = APPLICATION_CONTROL_COMMAND, .retained = true, .command_only = true,
        .command = *admission, .source_elapsed_ns = source_interval(command->kind, admission->elapsed_ns),
        .source_usercmd = true, .source_holdable = use_holdable};
    const control_input *input = &owner->inputs[admission->actor.slot];
    if (owner->q3_group && qa_actor_id_equal(owner->q3_group->actor, admission->actor)) {
        current.arsenal = owner->q3_group->arsenal; current.weapon = owner->q3_group->weapon;
    } else if (qa_actor_id_equal(input->actor, admission->actor)) {
        current.arsenal = input->arsenal; current.weapon = input->weapon;
    }
    const application_control_context *previous = owner->current;
    owner->current = &current;
    bool ok = application_control_move_applied(app, admission->actor, command, NULL, error);
    owner->current = previous;
    if (ok && qa_actor_id_equal(input->actor, admission->actor) &&
        (!owner->q3_group || input->sequence == owner->q3_group->commands[0].sequence)) {
        owner->inputs[admission->actor.slot].arsenal = 0;
        owner->inputs[admission->actor.slot].weapon = 0;
    }
    return ok;
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
    return owner && owner->state && qa_actor_id_equal(owner->state_actor, actor) &&
        application_control_frame_current(app, actor) ? owner->state : NULL;
}

bool application_control_frames_owns(const qa_application *app, qa_actor_id actor)
{
    if (!app || !app->control_frames || actor.slot >= app->control_capacity) return false;
    const application_control_record *record = &app->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor)) return false;
    application_provider *provider = source_provider((qa_application *)app, actor);
    qa_source_frame frame;
    if (!source_client(provider) || !qa_session_active_frame(app->session, provider->owner, &frame)) return false;
    const control_input *input = &app->control_frames->inputs[actor.slot];
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
} control_apply;

static bool drain(qa_application *, const qa_source_frame *, size_t, bool, bool, qa_error *);

static bool invoke_apply(void *opaque, qa_session *session, qa_error *error)
{
    control_apply *call = opaque;
    if (session != call->application->session)
        return application_fail(error, QA_ERROR_ARGUMENT, "Applied command invocation lost its session");
    if (!qa_actors_get(qa_session_actors(session), call->actor)) return true;
    return application_control_stage_move(call->application, call->actor, call->command, call->turn, error);
}

static bool apply(qa_application *app, qa_actor_id actor, const qa_movement_command *command,
                    const qa_source_frame *frame, application_control_source_path path,
                    application_control_stage stage, bool retained, bool defer_postthink,
                    struct application_control_turn **turn, qa_actor_owner arsenal, qa_item_id weapon, qa_error *error)
{
    struct application_control_frames *owner = app->control_frames;
    application_control_context current = {actor, *frame, path, stage, retained, defer_postthink};
    current.arsenal = arsenal; current.weapon = weapon;
    uint64_t host_elapsed = frame->elapsed_ns;
    (void)qa_session_advance_interval(app->session, &host_elapsed);
    current.source_elapsed_ns = source_interval(command->kind, host_elapsed);
    const application_control_context *previous = owner->current;
    owner->current = &current;
    control_input *input = &owner->inputs[actor.slot];
    if (qa_actor_id_equal(input->actor, actor) && input->provider == frame->provider) {
        input->frame_owned = true; input->owned_frame_number = frame->number;
    }
    control_apply call = {app, actor, command, turn, arsenal, weapon};
    const qa_invocation *invocation = qa_session_current(app->session);
    bool existing_turn = stage == APPLICATION_CONTROL_PHYSICS && invocation &&
        invocation->kind == QA_INVOKE_PHYSICS && qa_actor_id_equal(invocation->actor, actor);
    bool ok = existing_turn ? invoke_apply(&call, app->session, error) :
        qa_session_invoke(app->session, actor, QA_INVOKE_PHYSICS, invoke_apply, &call, error);
    owner->current = previous;
    return ok;
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
        if (!record->active || !qa_actors_get(qa_session_actors(session), record->actor)) continue;
        application_provider *provider = source_provider(app, record->actor);
        if (!source_client(provider) || provider->component.clock.kind != QA_CLOCK_NETQUAKE ||
            record->state.kind != QA_MOVEMENT_NETQUAKE) continue;
        const qa_source_frame *frame = actor_frame(app, record->actor, frames, count);
        if (!frame) continue;
        control_input *input = &app->control_frames->inputs[i];
        if (!qa_actor_id_equal(input->actor, record->actor) && input->turn) {
            struct application_control_turn *turn = input->turn; input->turn = NULL;
            if (!application_control_turn_abort(turn, error)) return false;
        }
        if (!qa_actor_id_equal(input->actor, record->actor)) *input = (control_input){.actor = record->actor,
            .provider = provider->owner, .retained = true};
        if (input->provider != provider->owner || input->turn)
            return application_fail(error, QA_ERROR_ARGUMENT, "Retained input execution changed during admission");
        qa_movement_command command = input->seen ? input->latest :
            (qa_movement_command){.kind = QA_MOVEMENT_NETQUAKE, .angles = record->command_angles};
        command.impulse = input->impulse;
        command.milliseconds = (uint32_t)(frame->elapsed_ns / UINT64_C(1000000));
        if (!apply(app, record->actor, &command, frame, APPLICATION_CONTROL_NQ_TURN,
            APPLICATION_CONTROL_PREPARE, true, false, &input->turn, input->arsenal, input->weapon, error)) return false;
        input->arsenal = 0; input->weapon = 0;
    }
    return drain(app, frames, count, false, true, error);
}

typedef struct command_group_call {
    qa_application *app;
    control_group *group;
    size_t index;
    bool post;
} command_group_call;

static int32_t qw_angle_word(float value)
{
    return (int32_t)(uint16_t)(int32_t)(fmod((double)value, 360.0) * 65536.0 / 360.0);
}

static float qw_axis(float value)
{
    double scaled = (double)value * 127.0 / 320.0;
    return (float)trunc(scaled < -127 ? -127 : scaled > 127 ? 127 : scaled);
}

static bool qw_selected_command(qa_application *app, qa_actor_id actor,
    const qa_movement_command *raw, const application_control_context *context,
    qa_movement_command *out, qa_error *error)
{
    application_control_record *record = &app->controls[actor.slot];
    float forward = qw_axis(raw->forward_move), side = qw_axis(raw->side_move);
    float up = qw_axis(raw->buttons & 2u ? 320.0f : raw->up_move);
    int32_t words[] = {qw_angle_word(raw->angles.x), qw_angle_word(raw->angles.y), qw_angle_word(raw->angles.z)};
    *out = (qa_movement_command){.kind = record->state.kind, .sequence = raw->sequence,
        .milliseconds = raw->milliseconds, .buttons = raw->buttons & 1u,
        .angles = {(float)(words[0] * 360.0 / 65536.0), (float)(words[1] * 360.0 / 65536.0),
            (float)(words[2] * 360.0 / 65536.0)}};
    uint32_t time = record->state.kind == QA_MOVEMENT_Q3 ?
        (uint32_t)record->state.data.q3.command_time_ms : (uint32_t)(context->command.time_ns / UINT64_C(1000000));
    time += raw->milliseconds; memcpy(&out->server_time_ms, &time, sizeof(time));
    out->acknowledged_server_seconds = (double)out->server_time_ms / 1000.0;
    double speed = out->kind == QA_MOVEMENT_NETQUAKE ? 320.0 : out->kind == QA_MOVEMENT_Q3 ? 127.0 : 200.0;
    out->forward_move = (float)((double)forward * speed / 127.0);
    out->side_move = (float)((double)side * speed / 127.0);
    out->up_move = (float)((double)up * speed / 127.0);
    if (out->kind == QA_MOVEMENT_NETQUAKE) {
        if (up > 0) out->buttons |= 2u;
    } else if (out->kind == QA_MOVEMENT_Q2_RERELEASE) {
        if (up > 0) out->buttons |= 8u;
        if (up < 0) out->buttons |= 16u;
        out->up_move = 0; out->angles = qa_vec_sub(out->angles, record->state.data.q2r.delta_angles);
    } else for (size_t i = 0; i < 3; ++i) {
        int32_t delta = out->kind == QA_MOVEMENT_Q3 ? record->state.data.q3.delta_angle_words[i] :
            record->state.data.q2.delta_angle_shorts[i];
        uint32_t bits = (uint32_t)words[i] - (uint32_t)delta;
        memcpy(&out->angle_words[i], &bits, sizeof(bits));
    }
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

static bool qw_foreign_slice(command_group_call *call, application_control_context *current,
    qa_movement_command raw, uint32_t maximum, qa_error *error)
{
    if (raw.milliseconds > maximum) {
        raw.milliseconds /= 2;
        if (!qw_foreign_slice(call, current, raw, maximum, error)) return false;
        raw.impulse = 0;
        return !qa_actors_get(qa_session_actors(call->app->session), call->group->actor) ||
            qw_foreign_slice(call, current, raw, maximum, error);
    }
    qa_application *app = call->app; qa_actor_id actor = call->group->actor;
    application_source_input_scope scope = {0};
    current->source_elapsed_ns = (uint64_t)raw.milliseconds * UINT64_C(1000000);
    current->source_command = raw;
    bool ok = application_control_source_input(app, actor, &app->controls[actor.slot].state,
        &raw, NULL, &scope, true, true, current->source_elapsed_ns, error);
    if (ok && qa_actors_get(qa_session_actors(app->session), actor)) {
        current->source_command = raw;
        qa_movement_command selected;
        ok = qw_selected_command(app, actor, &raw, current, &selected, error) &&
            application_control_stage_move(app, actor, &selected, NULL, error);
    }
    if (ok && qa_actors_get(qa_session_actors(app->session), actor))
        ok = application_control_source_input(app, actor, &app->controls[actor.slot].state,
            &raw, NULL, &scope, false, true, current->source_elapsed_ns, error);
    qa_error cleanup = {0};
    if (!application_control_source_abort(&scope, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    return ok;
}

static bool qw_foreign_command(command_group_call *call, application_control_context *current, qa_error *error)
{
    qa_application *app = call->app; qa_actor_id actor = call->group->actor;
    qa_movement_command raw = call->group->commands[call->index];
    qa_movement_profile source = qa_movement_profile_default(QA_MOVEMENT_QUAKEWORLD);
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
    bool ok = application_control_source_input(app, actor, &app->controls[actor.slot].state,
        &raw, NULL, &scope, true, false, current->source_elapsed_ns, error);
    if (ok && qa_actors_get(qa_session_actors(app->session), actor))
        ok = qw_foreign_slice(call, current, raw, maximum, error);
    if (ok && qa_actors_get(qa_session_actors(app->session), actor))
        ok = application_control_source_input(app, actor, &app->controls[actor.slot].state,
            &raw, NULL, &scope, false, false, (uint64_t)elapsed * UINT64_C(1000000), error);
    qa_error cleanup = {0};
    if (!application_control_source_abort(&scope, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    return ok;
}

static bool apply_command_group(void *opaque, qa_session *session, const qa_source_command *command, qa_error *error)
{
    command_group_call *call = opaque;
    qa_application *app = call->app; control_group *group = call->group;
    if (session != app->session || group->provider != command->provider ||
        !qa_actor_id_equal(group->actor, command->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Command group lost its genuine source admission");
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
    if (group->domain == CONTROL_COMMAND_Q3_SOURCE) {
        current.source_guestcmd = true; current.retained = true;
        current.source_elapsed_ns = source_interval(app->controls[group->actor.slot].state.kind,
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
        control_input *input = &owner->inputs[group->actor.slot];
        qa_source_frame frame;
        if (qa_actor_id_equal(input->actor, group->actor) && input->provider == group->provider &&
            qa_session_active_frame(session, group->provider, &frame)) {
            input->frame_owned = true; input->owned_frame_number = frame.number;
        }
        ok = group->domain == CONTROL_COMMAND_QW_SOURCE && app->controls[group->actor.slot].state.kind != QA_MOVEMENT_QUAKEWORLD
            ? qw_foreign_command(call, &current, error)
            : application_control_stage_move(app, group->actor, &group->commands[call->index], NULL, error);
    }
    owner->current = previous;
    return ok;
}

static bool execute_original_q3_group(qa_application *app, control_group *group, qa_error *error)
{
    command_group_call call = {app, group, 0, false};
    application_operation previous = app->operation;
    app->operation = APPLICATION_ADVANCING;
    bool ok = qa_session_command_call(app->session, group->provider, group->actor, 0,
        apply_command_group, &call, error);
    app->operation = previous;
    if (ok && qa_actors_get(qa_session_actors(app->session), group->actor)) {
        application_control_record *record = &app->controls[group->actor.slot];
        record->command_sequence = group->commands[0].sequence; record->command_seen = true;
    }
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
            bool later_native_q3 = provider && provider->component.clock.kind == QA_CLOCK_Q3 &&
                !actor_frame(app, candidate->actor, frames, count) && qa_session_frame_pending(app->session, provider->owner);
            if (!candidate_live || (candidate->quakeworld == quakeworld && candidate->before_source == before &&
                (!quakeworld || actor_frame(app, candidate->actor, frames, count)) && !later_native_q3)) break;
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
        uint64_t host_elapsed = frame ? frame->elapsed_ns : 0;
        (void)qa_session_advance_interval(app->session, &host_elapsed);
        for (size_t i = 0; ok && live && i < group->count; ++i) {
            if (group->domain == CONTROL_COMMAND_Q3_SOURCE && provider->kind == APPLICATION_PROVIDER_Q3) {
                qa_q3_usercmd raw = q3_source_command(&group->commands[i], true);
                owner->q3_group = group;
                ok = application_control_q3_client_think(provider, group->actor, &raw, error);
                owner->q3_group = NULL;
                live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
                if (ok && live) {
                    application_control_record *record = &app->controls[group->actor.slot];
                    record->command_sequence = group->commands[i].sequence; record->command_seen = true;
                }
                continue;
            }
            uint64_t elapsed = group->commands[i].kind == QA_MOVEMENT_NETQUAKE
                ? source_interval(group->commands[i].kind, host_elapsed)
                : (uint64_t)group->commands[i].milliseconds * UINT64_C(1000000);
            command_group_call call = {app, group, i, false};
            ok = qa_session_command_call(app->session, group->provider, group->actor, elapsed,
                apply_command_group, &call, error);
            live = qa_actors_get(qa_session_actors(app->session), group->actor) != NULL;
            if (ok && live && group->domain == CONTROL_COMMAND_Q3_SOURCE) {
                application_control_record *record = &app->controls[group->actor.slot];
                record->command_sequence = group->commands[i].sequence; record->command_seen = true;
            }
        }
        if (ok && live && quakeworld) {
            uint64_t elapsed = (uint64_t)group->commands[group->count - 1].milliseconds * UINT64_C(1000000);
            command_group_call call = {app, group, group->count - 1, true};
            ok = qa_session_command_call(app->session, group->provider, group->actor, elapsed,
                apply_command_group, &call, error);
        }
        if (ok && live && group->domain == CONTROL_COMMAND_QW_SOURCE) {
            application_control_record *record = &app->controls[group->actor.slot];
            record->command_sequence = group->commands[group->count - 1].sequence;
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
        map->component.clock.kind == QA_CLOCK_NETQUAKE;
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
        if (!record->active || record->state.kind == QA_MOVEMENT_NETQUAKE ||
            !qa_q1_player_source_present(map->state.q1, record->actor)) continue;
        const qa_movement_command *received = NULL;
        for (const control_group *group = app->control_frames->head; group; group = group->next)
            if (!group->bot && qa_actor_id_equal(group->actor, record->actor))
                received = &group->commands[group->count - 1];
        control_input *input = &app->control_frames->inputs[i];
        if (received && (!qa_actor_id_equal(input->actor, record->actor) || !input->seen))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source preparation lost its received command");
        if (received && applications_active) {
            qa_source_frame actual;
            if (!qa_session_active_frame(app->session, map->owner, &actual))
                return application_fail(error, QA_ERROR_ARGUMENT, "Deferred Q1 source input lost its real map frame");
            input->q1_source_actor = record->actor; input->q1_source_provider = map->owner;
            input->q1_source_frame_number = actual.number; input->q1_source_sequence = received->sequence;
            input->q1_source_deferred = true;
            continue;
        }
        if (!application_control_q1_source_prethink(app, record->actor, received, error)) return false;
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
        qa_actor_id_equal(app->controls[actor.slot].actor, actor) &&
        app->controls[actor.slot].state.kind == QA_MOVEMENT_NETQUAKE &&
        provider->component.clock.kind == QA_CLOCK_NETQUAKE &&
        provider->component.command_actor &&
        provider->component.command_actor(provider->component.state, session, actor);
    *handled = reserved || physical_nq || application_control_frames_owns(app, actor);
    if (!*handled) return true;
    if (physical_nq && frame->provider != provider->owner) return true;
    control_input *input = &app->control_frames->inputs[actor.slot];
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
        (void)application_control_turn_abort(turn, NULL);
        return false;
    }
    if (!qa_actors_get(qa_session_actors(session), actor)) {
        struct application_control_turn *turn = input->turn; input->turn = NULL;
        bool okay = application_control_turn_abort(turn, error);
        *input = (control_input){0};
        return okay;
    }
    if (!input->turn || !qa_actor_id_equal(input->actor, actor)) {
        input->source_turn_actor = actor; input->source_turn_provider = frame->provider;
        input->source_turn_frame_number = frame->number;
        return true;
    }
    qa_movement_command command = input->seen ? input->latest : (qa_movement_command){.kind = QA_MOVEMENT_NETQUAKE};
    command.impulse = input->impulse;
    bool ok = apply(app, actor, &command, frame, APPLICATION_CONTROL_NQ_TURN,
        APPLICATION_CONTROL_PHYSICS, true, false, &input->turn, 0, 0, error);
    if (ok) {
        input->impulse = 0;
        input->source_turn_actor = actor; input->source_turn_provider = frame->provider;
        input->source_turn_frame_number = frame->number;
    }
    if (!qa_actors_get(qa_session_actors(session), actor)) *input = (control_input){0};
    return ok;
}

bool application_control_frames_end(void *opaque, qa_session *session, const qa_source_frame *frames,
                                     size_t count, uint64_t host_ns, qa_error *error)
{
    (void)host_ns; qa_application *app = opaque;
    if (!app || session != app->session || !app->control_frames)
        return application_fail(error, QA_ERROR_ARGUMENT, "Command completion lost its source owner");
    for (uint32_t i = 0; i < app->control_frames->capacity; ++i) {
        control_input *input = &app->control_frames->inputs[i];
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
    for (uint32_t i = 0; i < app->control_frames->capacity; ++i) {
        app->control_frames->inputs[i].frame_owned = false;
        app->control_frames->inputs[i].owned_frame_number = 0;
    }
    return ok;
}

static bool command_fields(qa_source_save_io *io, qa_movement_command *command)
{
    uint32_t kind = command->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_MOVEMENT_Q3 ||
        !qa_source_save_u64(io, &command->sequence) || !qa_source_save_u32(io, &command->milliseconds) ||
        !qa_source_save_i32(io, &command->server_time_ms) || !qa_source_save_i32(io, &command->server_frame) ||
        !qa_source_save_f64(io, &command->acknowledged_server_seconds) || !qa_source_save_vec3(io, &command->angles)) return false;
    for (unsigned i = 0; i < 3; ++i) if (!qa_source_save_i32(io, &command->angle_words[i])) return false;
    if (!qa_source_save_f32(io, &command->forward_move) || !qa_source_save_f32(io, &command->side_move) ||
        !qa_source_save_f32(io, &command->up_move) || !qa_source_save_u32(io, &command->buttons) ||
        !qa_source_save_u8(io, &command->impulse) || !qa_source_save_u8(io, &command->light_level) ||
        !qa_source_save_u8(io, &command->weapon)) return false;
    command->kind = (qa_movement_kind)kind;
    return command_valid(command) || application_fail(io->error, QA_ERROR_FORMAT, "Saved input command contains invalid values");
}

static bool domain_fields(qa_source_save_io *io, control_command_domain *domain, uint64_t *time_ns)
{
    uint8_t value = (uint8_t)*domain;
    if (!qa_source_save_u8(io, &value) || value > CONTROL_COMMAND_Q3_SOURCE ||
        !qa_source_save_u64(io, time_ns)) return false;
    *domain = (control_command_domain)value;
    return true;
}

static bool domain_owner(qa_application *app, application_provider *source, qa_actor_id actor,
    control_command_domain domain, uint64_t time_ns, bool reading)
{
    if (!source) return false;
    if (domain == CONTROL_COMMAND_SELECTED) return time_ns == 0;
    if (source != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !saved_source_client(source, actor, NULL)) return false;
    qa_clock_state clock;
    if (!reading && (!qa_session_clock(app->session, source->owner, &clock) || time_ns > clock.frame.time_ns)) return false;
    if (domain == CONTROL_COMMAND_Q3_SOURCE)
        return source->component.clock.kind == QA_CLOCK_Q3 &&
            (source->kind == APPLICATION_PROVIDER_Q3 || original_q3(source));
    return source_client(source) && source->component.clock.kind == QA_CLOCK_QUAKEWORLD;
}

static bool domain_command(control_command_domain domain, const qa_movement_command *command,
    const application_control_record *control)
{
    if (domain == CONTROL_COMMAND_Q3_SOURCE) return q3_raw_valid(command);
    if (domain == CONTROL_COMMAND_QW_SOURCE)
        return command_valid(command) && command->kind == QA_MOVEMENT_QUAKEWORLD && command->milliseconds <= 255;
    return command->kind == control->state.kind &&
        (command->kind != QA_MOVEMENT_QUAKEWORLD || command->milliseconds <= 255);
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
    if (!reading && owner) for (uint32_t i = 0; i < owner->capacity; ++i) if (owner->inputs[i].actor.registry) ++count;
    bool ok = qa_source_save_count(io, &count, app->control_capacity);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        control_input value = {0};
        if (!reading) {
            while (previous < owner->capacity && !owner->inputs[previous].actor.registry) ++previous;
            value = owner->inputs[previous++];
        }
        ok = qa_source_save_actor(io, &value.actor) && qa_source_save_string(io, &value.provider) &&
            qa_source_save_bool(io, &value.seen) && qa_source_save_bool(io, &value.retained) &&
            qa_source_save_u64(io, &value.sequence) && qa_source_save_u8(io, &value.impulse) &&
            qa_source_save_string(io, &value.arsenal) && qa_source_save_string(io, &value.weapon) &&
            domain_fields(io, &value.domain, &value.source_time_ns) &&
            qa_source_save_u64(io, &value.qw_receipt_time_ns) &&
            qa_source_save_bool(io, &value.has_body_base);
        if (ok && value.has_body_base) ok = qa_source_save_vec3(io, &value.body_base.mins) &&
            qa_source_save_vec3(io, &value.body_base.maxs);
        if (ok && value.seen) ok = command_fields(io, &value.latest);
        if (!ok) break;
        application_provider *source = saved_source_provider(app, value.actor, error);
        if (value.actor.slot >= app->control_capacity || !controls[value.actor.slot].active ||
            !qa_actor_id_equal(controls[value.actor.slot].actor, value.actor) ||
            !source || source->owner != value.provider || !domain_owner(app, source, value.actor, value.domain, value.source_time_ns, reading) ||
            (value.domain == CONTROL_COMMAND_QW_SOURCE ? value.qw_receipt_time_ns > value.source_time_ns :
                value.qw_receipt_time_ns != 0) ||
            (reading && i && value.actor.slot <= previous) ||
            (value.seen && controls[value.actor.slot].command_seen && value.sequence < controls[value.actor.slot].command_sequence) ||
            (!value.seen && (value.sequence || value.impulse)) || (!value.retained && value.impulse) ||
            (!value.retained && ((!value.seen && !value.has_body_base) ||
                (value.domain != CONTROL_COMMAND_Q3_SOURCE && (value.arsenal || value.weapon)))) ||
            (value.has_body_base && (!body_base_owner(source, controls[value.actor.slot].state.kind) ||
                !qa_vec_finite(value.body_base.mins) || !qa_vec_finite(value.body_base.maxs) ||
                value.body_base.mins.x > value.body_base.maxs.x || value.body_base.mins.y > value.body_base.maxs.y ||
                value.body_base.mins.z > value.body_base.maxs.z)) ||
            ((value.arsenal || value.weapon) &&
                (!application_player_bot(app, value.actor) || !application_bots_actor(app, value.actor))) ||
            !weapon_owner(app, value.actor, value.arsenal, value.weapon) ||
            (value.seen && (!domain_command(value.domain, &value.latest, &controls[value.actor.slot]) ||
                value.latest.sequence != value.sequence)) ||
            (!value.seen && value.domain != CONTROL_COMMAND_SELECTED) ||
            (value.retained && value.seen && value.latest.impulse != 0)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved retained input has no exact control owner"); break;
        }
        bool retained = source_client(source) && source->component.clock.kind == QA_CLOCK_NETQUAKE &&
            controls[value.actor.slot].state.kind == QA_MOVEMENT_NETQUAKE;
        if (value.retained != retained) { ok = application_fail(error, QA_ERROR_FORMAT, "Saved input retention differs from source ownership"); break; }
        if (reading) { previous = value.actor.slot; owner->inputs[value.actor.slot] = value; }
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
        control_group *group = reading && ok ? calloc(1, sizeof(*group) + value.count * sizeof(*group->commands)) : source;
        if (ok && !group) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating saved input group");
        if (reading && ok) { *group = value; if (owner->tail) owner->tail->next = group; else owner->head = group; owner->tail = group; }
        application_provider *actor_source = ok ? saved_source_provider(app, value.actor, error) : NULL;
        if (ok && (value.actor.slot >= app->control_capacity || !controls[value.actor.slot].active ||
            !qa_actor_id_equal(controls[value.actor.slot].actor, value.actor) ||
            !actor_source || actor_source->owner != value.provider ||
            !domain_owner(app, actor_source, value.actor, value.domain, value.source_time_ns, reading) ||
            !qa_actor_id_equal(owner->inputs[value.actor.slot].actor, value.actor) ||
            owner->inputs[value.actor.slot].retained || (!value.bot && (value.arsenal || value.weapon)) ||
            !weapon_owner(app, value.actor, value.arsenal, value.weapon)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved input group has no exact control owner");
        bool qw = actor_source && source_client(actor_source) && actor_source->component.clock.kind == QA_CLOCK_QUAKEWORLD &&
            (value.domain == CONTROL_COMMAND_QW_SOURCE || controls[value.actor.slot].state.kind == QA_MOVEMENT_QUAKEWORLD);
        if (ok && (qw != value.quakeworld || before_source(actor_source) != value.before_source ||
            (qw && value.count > 20) || (value.domain == CONTROL_COMMAND_Q3_SOURCE &&
                (value.count != 1 || actor_source->kind != APPLICATION_PROVIDER_Q3)) ||
            (value.bot && (value.count != 1 ||
                !application_player_bot(app, value.actor) || !application_bots_actor(app, value.actor)))))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved command group differs from source ownership");
        uint64_t last = 0;
        bool seen = ok && controls[value.actor.slot].command_seen;
        if (seen) last = controls[value.actor.slot].command_sequence;
        if (ok) for (control_group *prior = owner->head; prior && prior != group; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, value.actor) && prior->count) {
                seen = true; last = prior->commands[prior->count - 1].sequence;
            }
        for (size_t j = 0; ok && j < value.count; ++j) {
            qa_movement_command command = reading ? (qa_movement_command){0} : group->commands[j];
            ok = command_fields(io, &command) && domain_command(value.domain, &command, &controls[value.actor.slot]) &&
                (j && value.quakeworld ? command.sequence == last : !seen || command.sequence > last) &&
                command.sequence <= owner->inputs[value.actor.slot].sequence;
            if (ok) { seen = true; last = command.sequence; if (reading) group->commands[j] = command; }
        }
        if (!reading && source) source = source->next;
    }
    for (uint32_t i = 0; ok && owner && i < owner->capacity; ++i) {
        const control_input *input = &owner->inputs[i];
        if (!input->actor.registry || input->retained) continue;
        const control_group *tail = NULL;
        for (const control_group *group = owner->head; group; group = group->next)
            if (qa_actor_id_equal(group->actor, input->actor)) tail = group;
        if (tail && (tail->commands[tail->count - 1].sequence != input->sequence ||
            tail->domain != input->domain || tail->source_time_ns != input->source_time_ns ||
            !command_equal(&tail->commands[tail->count - 1], &input->latest)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved input queue omits its actual latest received command");
    }
    if (!ok && reading) application_control_frames_free(owner);
    else if (ok && reading) *decoded = owner;
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid saved source input continuation");
    return ok;
}
