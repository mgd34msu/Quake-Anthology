#include "remote_prediction.h"
#include "qa/game_q3.h"
#include "qa/network_q3_fields_save.h"
#include "qa/persistence_fields.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct prediction_player {
    frontend_remote_prediction_view view;
    qa_movement_environment environment;
    float fractional_weapon_ms;
    int32_t arsenal_event_sequence;
    uint32_t external_slot;
    int32_t requested_weapon;
} prediction_player;
typedef struct prediction_command {
    qa_usercmd selected;
    qa_usercmd source;
    frontend_remote_prediction_angle_space angle_space;
    uint64_t source_sequence;
    uint64_t receipt_time_ns;
    int32_t source_time;
    uint32_t source_buttons;
    uint8_t source_weapon;
    prediction_player continuation;
    bool has_continuation;
} prediction_command;
typedef struct prediction_item {
    int32_t publication_message, source_misc_time, misc_time;
    bool seen, published, hidden;
} prediction_item;
typedef struct prediction_state {
    prediction_player baseline, predicted;
    prediction_command commands[64];
    size_t count;
    uint64_t discarded_sequence;
    bool has_discarded, initialized, valid_pps;
    qa_net_client_id connection;
    qa_actor_id actor;
    qa_actor_owner movement, character, arsenal;
    uint64_t epoch, map_identity, restart_generation;
    int32_t snapshot_number;
    qa_actor_owner receiver;
    uint64_t service_owner, receipt_time_ns;
    uint32_t launch_seat, source_client;
    int32_t presentation_time, physics_time;
    qa_vec3 q2r_pml_origin;
    qa_vec3 prediction_error;
    int32_t prediction_error_time;
    prediction_item items[QA_Q3_ENTITIES];
} prediction_state;
struct frontend_remote_prediction {
    frontend_remote_prediction_options options;
    prediction_player initial;
    prediction_state state;
    qa_movement_result scratch;
    const frontend_remote_prediction_cgame_seed *active_seed;
    bool busy;
};
typedef struct replay_context {
    frontend_remote_prediction *owner;
    const frontend_remote_prediction_source *source;
    prediction_player *player;
    const prediction_command *command;
} replay_context;

static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static int32_t signed_word(uint32_t word)
{ return word <= INT32_MAX ? (int32_t)word : (int32_t)((int64_t)word - INT64_C(4294967296)); }
static int32_t add_word(int32_t a, int32_t b)
{ return signed_word((uint32_t)a + (uint32_t)b); }
static int32_t subtract_word(int32_t a, int32_t b)
{ return signed_word((uint32_t)a - (uint32_t)b); }
static const qa_q3_snapshot *seed_snapshot(const frontend_remote_prediction_source *source)
{
    return source->scene.prediction_snapshot;
}
static bool receipt_current(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{
    const frontend_remote_prediction_cgame_seed *seed = owner->active_seed;
    return owner->options.source_current(owner->options.context, source) &&
        (!seed || seed->current(seed->context, source, seed));
}
static const qa_q3_snapshot *current_snapshot(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{ return owner->active_seed ? owner->active_seed->snapshot : source->scene.snapshot; }
static const qa_q3_snapshot *next_snapshot(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{ return owner->active_seed ? owner->active_seed->next_snapshot : source->scene.next_snapshot; }
static const qa_q3_snapshot *player_snapshot(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{
    if (!owner->active_seed) return seed_snapshot(source);
    return seed_snapshot(source) == source->scene.snapshot ?
        owner->active_seed->snapshot : owner->active_seed->next_snapshot;
}
static bool this_teleport(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{ return owner->active_seed ? owner->active_seed->this_frame_teleport : source->scene.this_frame_teleport; }
static bool next_teleport(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{ return owner->active_seed ? owner->active_seed->next_frame_teleport : source->scene.next_frame_teleport; }
static bool retail_snapshot_matches(const qa_q3_snapshot *retail,
    const qa_q3_snapshot *raw, qa_error *error)
{
    if (!retail || !raw) return retail == raw;
    if (!retail->valid || !raw->valid || retail->message_number != raw->message_number ||
        retail->server_time != raw->server_time || retail->player.product != raw->player.product)
        return false;
    qa_q3_player player = retail->player;
    player.entityEventSequence = raw->player.entityEventSequence;
    uint8_t a[1024], b[1024];
    qa_net_writer wa, wb;
    qa_net_writer_init(&wa, a, sizeof(a), error);
    qa_net_writer_init(&wb, b, sizeof(b), error);
    return qa_q3_save_player_fields(&wa, &player) &&
        qa_q3_save_player_fields(&wb, &raw->player) &&
        qa_net_writer_size(&wa) == qa_net_writer_size(&wb) &&
        !memcmp(a, b, qa_net_writer_size(&wa));
}
static bool retail_seed_valid(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source,
    const frontend_remote_prediction_cgame_seed *seed, qa_error *error)
{
    if (!seed || !seed->owner || !seed->scope || !seed->current ||
        !owner->options.source_current(owner->options.context, source) ||
        !seed->current(seed->context, source, seed)) return false;
    if (!retail_snapshot_matches(seed->snapshot, source->scene.snapshot, error) ||
        !retail_snapshot_matches(seed->next_snapshot, source->scene.next_snapshot, error)) return false;
    const qa_q3_snapshot *raw = seed->next_snapshot && !seed->next_frame_teleport &&
        !seed->this_frame_teleport ? source->scene.next_snapshot : source->scene.snapshot;
    return raw == seed_snapshot(source) && raw->server_time == source->scene.physics_time &&
        seed->current(seed->context, source, seed) &&
        owner->options.source_current(owner->options.context, source);
}
static qa_vec3 vector(const float value[3])
{ return qa_v3(value[0], value[1], value[2]); }
static void store_vector(float value[3], qa_vec3 source)
{ value[0] = source.x; value[1] = source.y; value[2] = source.z; }
static float angle(int32_t word)
{ uint32_t low = (uint32_t)word & 65535u; return (float)(low >= 32768u ? (int32_t)low - 65536 : (int32_t)low) * (360.0f / 65536.0f); }
static uint32_t stat_weapons(qa_q3_product product)
{ return product == QA_Q3_TEAM_ARENA ? 3u : 2u; }
static uint32_t stat_max_health(qa_q3_product product)
{ return product == QA_Q3_TEAM_ARENA ? 7u : 6u; }
static int32_t item_tag(qa_q3_product product, int32_t index)
{
    size_t count;
    const qa_q3_item *items = qa_q3_items(product, &count);
    return index >= 0 && (size_t)index < count ? items[index].tag : 0;
}
static void event(prediction_player *player, int32_t value)
{
    qa_q3_player *p = &player->view.player;
    uint32_t slot = (uint32_t)p->eventSequence & 1u;
    p->events[slot] = value; p->eventParms[slot] = 0;
    p->eventSequence = add_word(p->eventSequence, 1);
}
static void event_parameter(prediction_player *player, int32_t value, int32_t parameter)
{
    event(player, value);
    player->view.player.eventParms[((uint32_t)player->view.player.eventSequence - 1u) & 1u] = parameter;
    if (player->view.movement.kind == QA_RULESET_Q3)
        ++player->view.movement.data.q3.event_sequence;
}
static void torso(prediction_player *player, int32_t value, bool continuing, bool dead)
{
    qa_q3_player *p = &player->view.player;
    if (dead ||
        (continuing && ((p->torsoAnim & ~128) == value || p->torsoTimer > 0))) return;
    p->torsoAnim = ((p->torsoAnim & 128) ^ 128) | value;
}
static void legs(prediction_player *player, int32_t value, bool force, bool dead)
{
    qa_q3_player *p = &player->view.player;
    int32_t timer = force ? 0 : p->legsTimer;
    if (dead || timer > 0 || (!force && (p->legsAnim & ~128) == value)) {
        p->legsTimer = timer; return;
    }
    p->legsTimer = timer;
    p->legsAnim = ((p->legsAnim & 128) ^ 128) | value;
}
static void drop_timers(prediction_player *player, uint32_t elapsed)
{
    qa_q3_player *p = &player->view.player;
    if (p->legsTimer > 0) p->legsTimer = (uint64_t)p->legsTimer > elapsed ? p->legsTimer - (int32_t)elapsed : 0;
    if (p->torsoTimer > 0) p->torsoTimer = (uint64_t)p->torsoTimer > elapsed ? p->torsoTimer - (int32_t)elapsed : 0;
}
static bool weapon_owned(const qa_q3_player *p, int32_t weapon)
{
    return weapon > 0 && weapon < (p->product == QA_Q3_TEAM_ARENA ? 14 : 11) &&
        ((uint32_t)p->stats[stat_weapons(p->product)] & (1u << (uint32_t)weapon));
}
static void begin_drop(prediction_player *player, bool animate)
{
    event(player, 22);
    player->view.player.weaponState = 2;
    player->view.player.weaponTime = add_word(player->view.player.weaponTime, 200);
    if (animate) torso(player, 9, false, player->environment.health <= 0);
}
static void finish_change(prediction_player *player, int32_t requested, bool animate)
{
    qa_q3_player *p = &player->view.player;
    p->weapon = weapon_owned(p, requested) ? requested : 0;
    p->weaponState = 1; p->weaponTime = add_word(p->weaponTime, 250);
    if (animate) torso(player, 10, false, player->environment.health <= 0);
}
/* PM_Weapon operates on the copied native PS and private fractional/slot
 * continuation. It never reaches GAME inventory, events or holdable code. */
static bool weapon(replay_context *context, const qa_usercmd *command,
    uint32_t elapsed, qa_error *error)
{
    prediction_player *player = context->player;
    qa_q3_player *p = &player->view.player;
    bool animate = context->source->configuration.q3_character;
    bool attack = (command->buttons & 1u) != 0;
    bool use = ((command->kind == QA_RULESET_Q3 ? command->buttons :
        context->command->source_buttons) & 4u) != 0;
    if (player->environment.health > 0 && !attack && !use) p->pmFlags &= ~512;
    double clock = (double)elapsed + player->fractional_weapon_ms;
    if (!isfinite(clock) || clock < 0 || clock > INT32_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Private Q3 weapon clock exceeds its actual integer domain");
    int32_t msec = (int32_t)clock;
    player->fractional_weapon_ms = (float)(clock - msec);
    /* The presentation adapter's actual private arsenal snapshot uses
     * pmType == 1 for its spectator flag, independently of trigger PM enums. */
    if ((p->pmFlags & 512) || p->pmType == 1) return true;
    if (player->environment.health <= 0) { p->weapon = 0; return true; }
    if (use && !(p->pmFlags & 1024)) {
        int32_t tag = item_tag(p->product, p->stats[1]);
        if (tag != 2 || (double)player->environment.health <
            (double)p->stats[stat_max_health(p->product)] + 25.0) {
            p->pmFlags |= 1024; event(player, 24 + tag); p->stats[1] = 0;
        }
        return true;
    }
    if (!use) p->pmFlags &= ~1024;
    if (p->weaponTime > 0) p->weaponTime = add_word(p->weaponTime, -msec);
    int32_t requested = player->requested_weapon >= 0 ? player->requested_weapon :
        command->kind == QA_RULESET_Q3 ? command->weapon : context->command->source_weapon;
    if (player->external_slot == 3) return true;
    if (player->external_slot == 2) {
        if (p->weaponTime <= 0) player->external_slot = 3;
        return true;
    }
    if (player->external_slot == 4) {
        if (p->weaponTime <= 0) { finish_change(player, requested, animate); player->external_slot = 0; }
        return true;
    }
    if (player->external_slot == 1 && p->weaponTime <= 0 &&
        (p->weaponState == 0 || p->weaponState == 3) &&
        !(requested != p->weapon && weapon_owned(p, requested))) {
        begin_drop(player, animate); player->external_slot = 2; return true;
    }
    if ((p->weaponTime <= 0 || p->weaponState != 3) && requested != p->weapon &&
        weapon_owned(p, requested) && p->weaponState != 2) begin_drop(player, animate);
    if (p->weaponTime > 0) return true;
    if (p->weaponState == 2) { finish_change(player, requested, animate); return true; }
    if (p->weaponState == 1) {
        p->weaponState = 0;
        if (animate) torso(player, p->weapon == 1 ? 12 : 11, false, player->environment.health <= 0);
        return true;
    }
    /* The actual client predictor supplies gauntletHit=false. */
    if (!attack || p->weapon == 1) { p->weaponTime = 0; p->weaponState = 0; return true; }
    if (animate) torso(player, p->weapon == 1 ? 8 : 7, false, player->environment.health <= 0);
    p->weaponState = 3;
    int32_t ammo = p->weapon == 1 || p->weapon == 10 ? -1 :
        p->weapon >= 0 && p->weapon < 16 ? p->ammo[p->weapon] : 0;
    if (!ammo) { event(player, 21); p->weaponTime = add_word(p->weaponTime, 500); return true; }
    if (ammo != -1) p->ammo[p->weapon] = add_word(ammo, -1);
    event(player, 23);
    static const int32_t delay[14] = {400,400,100,1000,800,800,50,1500,100,200,400,1000,800,30};
    int32_t add = p->weapon >= 0 && p->weapon < 14 ? delay[p->weapon] : 400;
    int32_t persistent = p->product == QA_Q3_TEAM_ARENA ? item_tag(p->product, p->stats[2]) : 0;
    if (persistent == 10) add = (int32_t)((double)add / 1.5);
    else if (persistent == 13 || player->environment.haste) add = (int32_t)((double)add / 1.3);
    p->weaponTime = add_word(p->weaponTime, add);
    return true;
}

static bool source_current(const replay_context *context, qa_error *error)
{
    return receipt_current(context->owner, context->source) ||
        fail(error, QA_ERROR_NOT_FOUND, "Private prediction lost its actual source receipt");
}
static bool trace(void *opaque, const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    replay_context *context = opaque;
    return source_current(context, error) && context->owner->options.trace(
        context->owner->options.context, context->source, query, out, error) && source_current(context, error);
}
static bool contents(void *opaque, const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    replay_context *context = opaque;
    return source_current(context, error) && context->owner->options.point_contents(
        context->owner->options.context, context->source, query, out, error) && source_current(context, error);
}
static bool is_bsp(void *opaque, const qa_trace_result *hit, bool *out, qa_error *error)
{
    replay_context *context = opaque;
    return source_current(context, error) && context->owner->options.is_bsp(
        context->owner->options.context, context->source, hit, out, error) && source_current(context, error);
}
static qa_movement_control touch(void *opaque, const qa_trace_result *hit,
    qa_movement_call *call, qa_error *error)
{
    (void)hit; (void)call;
    return source_current(opaque, error) ? QA_MOVEMENT_CONTINUE : QA_MOVEMENT_ERROR;
}
static bool firing(void *opaque, const qa_movement_call *call)
{
    replay_context *context = opaque;
    return (call->command->buttons & 1u) && context->player->environment.health > 0;
}
static qa_movement_control phase(void *opaque, qa_movement_phase value,
    qa_movement_call *call, qa_error *error)
{
    replay_context *context = opaque;
    if (!source_current(context, error)) return QA_MOVEMENT_ERROR;
    prediction_player *player = context->player;
    qa_q3_player *p = &player->view.player;
    bool character = context->source->configuration.q3_character;
    bool dead = call->state->kind == QA_RULESET_Q3 ?
        call->state->data.q3.movement_type >= 3 : player->environment.health <= 0;
    if (value == QA_MOVE_WEAPON && context->source->configuration.q3_arsenal) {
        uint32_t before = (uint32_t)p->eventSequence;
        if (!weapon(context, call->command, call->milliseconds, error)) return QA_MOVEMENT_ERROR;
        uint32_t emitted = (uint32_t)p->eventSequence - before;
        player->arsenal_event_sequence = signed_word((uint32_t)player->arsenal_event_sequence + emitted);
        if (player->requested_weapon == p->weapon) player->requested_weapon = -1;
        if (call->state->kind == QA_RULESET_Q3) {
            call->state->data.q3.movement_flags =
                (call->state->data.q3.movement_flags & ~(512u | 1024u)) | ((uint32_t)p->pmFlags & (512u | 1024u));
            call->state->data.q3.event_sequence += emitted;
        }
    } else if (character && value == QA_MOVE_DROP_TIMERS) drop_timers(player, call->milliseconds);
    else if (character && value == QA_MOVE_TORSO) torso(player, 11, true, dead);
    else if (character && value == QA_MOVE_GESTURE && p->torsoTimer == 0) {
        uint32_t buttons = call->command->buttons;
        if (buttons & 8u) {
            torso(player, 6, false, dead); p->torsoTimer = 34 * 66 + 50; event(player, 76);
            if (call->state->kind == QA_RULESET_Q3) ++call->state->data.q3.event_sequence;
        } else if (p->product == QA_Q3_TEAM_ARENA) {
            static const uint32_t gesture_buttons[] = {128,256,512,1024,32,64};
            static const int32_t gesture_animations[] = {25,26,27,28,29,30};
            for (size_t i = 0; i < sizeof(gesture_buttons) / sizeof(gesture_buttons[0]); ++i)
                if (buttons & gesture_buttons[i]) {
                    torso(player, gesture_animations[i], false, dead); p->torsoTimer = 600; break;
                }
        }
    }
    return QA_MOVEMENT_CONTINUE;
}
static qa_movement_control effect(void *opaque, const qa_movement_effect *value,
    qa_movement_call *call, qa_error *error)
{
    replay_context *context = opaque;
    if (!source_current(context, error)) return QA_MOVEMENT_ERROR;
    prediction_player *player = context->player;
    qa_q3_player *p = &player->view.player;
    bool character = context->source->configuration.q3_character;
    if (value->kind == QA_MOVE_EFFECT_EVENT && call->state->kind == QA_RULESET_Q3) {
        if (value->value != 14 || character) {
            uint32_t slot = (uint32_t)p->eventSequence & 1u;
            p->events[slot] = value->value; p->eventParms[slot] = value->parameter;
            p->eventSequence = add_word(p->eventSequence, 1);
        }
    } else if (character && value->kind == QA_MOVE_EFFECT_ANIMATION) {
        if (value->animation_kind == QA_MOVE_ANIMATION_LEGS_TIMER) p->legsTimer = value->value;
        else {
            int32_t animation = value->value;
            bool landing = false;
            if (value->animation_kind == QA_MOVE_ANIMATION_LOCOMOTION) {
                drop_timers(player, call->milliseconds);
                switch ((qa_movement_locomotion)value->value) {
                case QA_MOVE_IDLE: animation = 22; break;
                case QA_MOVE_WALK: animation = value->backwards ? 33 : 14; break;
                case QA_MOVE_RUN: animation = value->backwards ? 16 : 15; break;
                case QA_MOVE_BACKWARD: animation = 16; break;
                case QA_MOVE_CROUCH: animation = value->backwards ? 32 : 13; break;
                case QA_MOVE_JUMP: animation = value->backwards ? 20 : 18; break;
                case QA_MOVE_LAND: animation = value->backwards ? 21 : 19; landing = true; break;
                case QA_MOVE_SWIM: animation = 17; break;
                }
            }
            bool dead = call->state->kind == QA_RULESET_Q3 ?
                call->state->data.q3.movement_type >= 3 : player->environment.health <= 0;
            legs(player, animation, value->force, dead);
            if (landing) p->legsTimer = 130;
        }
    }
    return QA_MOVEMENT_CONTINUE;
}

static bool source_ground(frontend_remote_prediction *owner, int32_t number,
    qa_movement_ground *out, qa_error *error)
{
    if (number == QA_Q3_ENTITY_WORLD) { *out = (qa_movement_ground){.hit = QA_TRACE_HIT_WORLD}; return true; }
    *out = (qa_movement_ground){0};
    if (number == QA_Q3_ENTITY_NONE) return true;
    if (number < 0 || number >= QA_Q3_ENTITY_WORLD)
        return fail(error, QA_ERROR_FORMAT, "Prediction ground has no source entity number");
    qa_actor_id actor = {0}; bool present = false;
    if (!owner->options.actor_at(owner->options.context, (uint32_t)number, &actor, &present, error)) return false;
    if (present) *out = (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR, .actor = actor};
    return true;
}
static bool ground_number(frontend_remote_prediction *owner, qa_movement_ground ground,
    int32_t *out, qa_error *error)
{
    if (ground.hit == QA_TRACE_HIT_WORLD) { *out = QA_Q3_ENTITY_WORLD; return true; }
    *out = QA_Q3_ENTITY_NONE;
    if (ground.hit == QA_TRACE_HIT_NONE) return true;
    if (ground.hit != QA_TRACE_HIT_ACTOR)
        return fail(error, QA_ERROR_FORMAT, "Prediction ground has an invalid hit domain");
    uint32_t number = 0; bool present = false;
    if (!owner->options.number_of(owner->options.context, ground.actor, &number, &present, error)) return false;
    if (present) {
        if (number >= QA_Q3_ENTITY_WORLD)
            return fail(error, QA_ERROR_FORMAT, "Prediction actor has an invalid source entity number");
        *out = (int32_t)number;
    }
    return true;
}
static bool seed_player(frontend_remote_prediction *owner,
    const qa_q3_player *p, const prediction_player *base,
    prediction_player *out, qa_error *error)
{
    prediction_player player = *base;
    player.view.player = *p;
    player.view.command_time = p->commandTime;
    player.view.view_angles = vector(p->viewangles);
    player.view.view_height = (float)p->viewheight;
    player.environment.health = (float)p->stats[0];
    player.environment.flight = p->powerups[6] != 0;
    player.environment.haste = p->powerups[3] != 0;
    player.environment.invulnerable = p->product == QA_Q3_TEAM_ARENA && p->powerups[14] != 0;
    player.arsenal_event_sequence = p->eventSequence;
    qa_movement_state *state = &player.view.movement;
    if (state->kind == QA_RULESET_Q3) {
        qa_movement_ground ground;
        if (!source_ground(owner, p->groundEntityNum, &ground, error)) return false;
        qa_actor_id pad = {0}; bool present = false;
        if (p->jumppadEnt && (!owner->options.actor_at(owner->options.context,
                (uint32_t)p->jumppadEnt, &pad, &present, error))) return false;
        state->data.q3 = (qa_q3_movement_state){
            .command_time_ms = p->commandTime, .movement_type = p->pmType,
            .bob_cycle = p->bobCycle, .movement_flags = (uint32_t)p->pmFlags,
            .movement_time_ms = p->pmTime, .origin = vector(p->origin),
            .velocity = vector(p->velocity), .gravity = p->gravity, .speed = p->speed,
            .delta_angle_words = {p->deltaAngles[0], p->deltaAngles[1], p->deltaAngles[2]},
            .movement_direction = p->movementDir, .grapple_point = vector(p->grapplePoint),
            .flags = (uint32_t)p->eFlags, .view_angles = vector(p->viewangles),
            .view_height = (float)p->viewheight, .ground = ground,
            .event_sequence = (uint32_t)p->eventSequence, .jump_pad = present ? pad : (qa_actor_id){0},
            .movement_frame = p->pmoveFramecount, .jump_pad_frame = p->jumppadFrame,
        };
        player.view.ground = ground;
        player.environment.gravity_multiplier = 1;
    } else if (!qa_movement_set_origin(state, vector(p->origin), error) ||
        !qa_movement_set_velocity(state, vector(p->velocity), error)) return false;
    *out = player;
    return true;
}
static bool seed(frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source, const prediction_player *base,
    prediction_player *out, qa_error *error)
{
    if (!seed_player(owner, &player_snapshot(owner, source)->player, base, out, error)) return false;
    if (source->has_acknowledged_sequence && !source->history_unavailable)
        out->view.sequence = source->acknowledged_sequence;
    return true;
}
static qa_usercmd relative_command(const prediction_command *entry,
    const qa_movement_state *state)
{
    qa_usercmd command = entry->selected;
    if (entry->angle_space != FRONTEND_REMOTE_PREDICTION_ABSOLUTE) return command;
    if (state->kind == QA_RULESET_Q3 || state->kind == QA_RULESET_Q2_CLASSIC) {
        for (unsigned i = 0; i < 3; ++i) {
            int32_t delta = state->kind == QA_RULESET_Q3 ? state->data.q3.delta_angle_words[i] :
                state->data.q2.delta_angle_shorts[i];
            command.angle_words[i] = signed_word((uint32_t)command.angle_words[i] - (uint32_t)delta);
        }
    } else if (state->kind == QA_RULESET_Q2_RERELEASE)
        command.angles = qa_vec_sub(command.angles, state->data.q2r.delta_angles);
    return command;
}
static bool write_player(frontend_remote_prediction *owner, prediction_player *player, qa_error *error)
{
    qa_q3_player *p = &player->view.player;
    const qa_movement_state *state = &player->view.movement;
    p->commandTime = player->view.command_time;
    store_vector(p->origin, qa_movement_origin(state));
    store_vector(p->velocity, qa_movement_velocity(state));
    store_vector(p->viewangles, player->view.view_angles);
    if (!isfinite(player->view.view_height) || player->view.view_height < INT32_MIN ||
        (double)player->view.view_height > INT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Prediction view height exceeds source PS domain");
    p->viewheight = (int32_t)player->view.view_height;
    if (!ground_number(owner, player->view.ground, &p->groundEntityNum, error)) return false;
    if (state->kind == QA_RULESET_Q3) {
        const qa_q3_movement_state *s = &state->data.q3;
        p->commandTime = s->command_time_ms; p->pmType = s->movement_type;
        p->pmFlags = signed_word(s->movement_flags); p->pmTime = s->movement_time_ms;
        p->bobCycle = s->bob_cycle; p->movementDir = s->movement_direction;
        for (unsigned i = 0; i < 3; ++i) p->deltaAngles[i] = s->delta_angle_words[i];
        p->eFlags = signed_word(s->flags); p->pmoveFramecount = s->movement_frame;
        p->jumppadFrame = s->jump_pad_frame; p->jumppadEnt = 0;
        if (s->jump_pad.registry) {
            uint32_t number = 0; bool present = false;
            if (!owner->options.number_of(owner->options.context, s->jump_pad, &number, &present, error)) return false;
            if (present) {
                if (number >= QA_Q3_ENTITY_WORLD) return fail(error, QA_ERROR_FORMAT, "Prediction jump pad lost its source number");
                p->jumppadEnt = (int32_t)number;
            }
        }
    }
    return true;
}
static bool update_angles(prediction_player *player, const prediction_command *entry, qa_error *error)
{
    qa_movement_state *state = &player->view.movement;
    qa_usercmd command = state->kind == QA_RULESET_Q3 ? entry->source : relative_command(entry, state);
    switch (state->kind) {
    case QA_RULESET_NETQUAKE:
        player->view.view_angles = state->data.nq.fix_angle ? state->data.nq.view_angles : command.angles;
        break;
    case QA_RULESET_QUAKEWORLD: player->view.view_angles = command.angles; break;
    case QA_RULESET_Q2_RERELEASE:
        player->view.view_angles = qa_vec_add(command.angles, state->data.q2r.delta_angles); break;
    case QA_RULESET_Q2_CLASSIC:
        player->view.view_angles = qa_v3(
            angle(add_word(command.angle_words[0], state->data.q2.delta_angle_shorts[0])),
            angle(add_word(command.angle_words[1], state->data.q2.delta_angle_shorts[1])),
            angle(add_word(command.angle_words[2], state->data.q2.delta_angle_shorts[2])));
        break;
    case QA_RULESET_Q3:
        if (!qa_q3_prediction_view(state, player->view.player.stats[0], &command, error)) return false;
        player->view.view_angles = state->data.q3.view_angles;
        break;
    }
    return true;
}
static float lerp_angle(float from, float to, float fraction)
{
    if (to - from > 180) to -= 360;
    if (to - from < -180) to += 360;
    return from + fraction * (to - from);
}
static bool interpolate(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    prediction_state *state, bool grab_angles, qa_error *error)
{
    const qa_q3_snapshot *snapshot = current_snapshot(owner, source);
    const qa_q3_player *a = &snapshot->player;
    prediction_player base = state->baseline;
    if (base.view.movement.kind != QA_RULESET_Q3 && base.view.command_time != a->commandTime) {
        bool found = false;
        for (size_t i = 0; i < state->count; ++i) {
            const prediction_command *entry = state->commands + i;
            if (entry->has_continuation && entry->continuation.view.command_time == a->commandTime) {
                base = entry->continuation; found = true;
            }
        }
        if (!found) return fail(error, QA_ERROR_NOT_FOUND, "Interpolation lost its selected continuation for the current source PS");
    }
    if (!seed_player(owner, a, &base, &state->predicted, error)) return false;
    state->predicted.view.hyperspace = state->predicted.view.consumed_teleport = false;
    if (grab_angles && state->count && !update_angles(&state->predicted, state->commands + state->count - 1, error)) return false;
    const qa_q3_snapshot *next = next_snapshot(owner, source);
    qa_vec3 origin = vector(a->origin), velocity = vector(a->velocity);
    if (!next_teleport(owner, source) && next && next->server_time > snapshot->server_time) {
        const qa_q3_player *b = &next->player;
        float fraction = (float)subtract_word(source->scene.time, snapshot->server_time) /
            (float)subtract_word(next->server_time, snapshot->server_time);
        origin = qa_vec_lerp(vector(a->origin), vector(b->origin), fraction);
        velocity = qa_vec_lerp(vector(a->velocity), vector(b->velocity), fraction);
        if (!qa_movement_set_origin(&state->predicted.view.movement, origin, error) ||
            !qa_movement_set_velocity(&state->predicted.view.movement, velocity, error)) return false;
        int32_t cycle = b->bobCycle < a->bobCycle ? add_word(b->bobCycle, 256) : b->bobCycle;
        float bob = (float)a->bobCycle + fraction * (float)subtract_word(cycle, a->bobCycle);
        int32_t bob_cycle = !isfinite(bob) || bob >= 2147483648.0f || bob < -2147483648.0f ? INT32_MIN : (int32_t)truncf(bob);
        state->predicted.view.player.bobCycle = bob_cycle;
        if (state->predicted.view.movement.kind == QA_RULESET_Q3)
            state->predicted.view.movement.data.q3.bob_cycle = bob_cycle;
        if (!grab_angles) state->predicted.view.view_angles = qa_v3(
            lerp_angle(a->viewangles[0], b->viewangles[0], fraction),
            lerp_angle(a->viewangles[1], b->viewangles[1], fraction),
            lerp_angle(a->viewangles[2], b->viewangles[2], fraction));
    }
    if (state->predicted.view.movement.kind == QA_RULESET_Q3)
        state->predicted.view.movement.data.q3.view_angles = state->predicted.view.view_angles;
    state->predicted.view.status = FRONTEND_REMOTE_PREDICTION_DISABLED;
    /* Interpolation starts with the complete current source PS. The selected
     * continuation supplies view input without projecting its contact or PM
     * fields back into that PS. Q3 view input can change delta pitch. */
    store_vector(state->predicted.view.player.viewangles, state->predicted.view.view_angles);
    if (grab_angles && state->predicted.view.movement.kind == QA_RULESET_Q3)
        for (unsigned i = 0; i < 3; ++i)
            state->predicted.view.player.deltaAngles[i] = state->predicted.view.movement.data.q3.delta_angle_words[i];
    /* Source interpolation remains binary32 even when the selected provider
     * keeps classic short coordinates for its next movement command. */
    store_vector(state->predicted.view.player.origin, origin);
    store_vector(state->predicted.view.player.velocity, velocity);
    return true;
}
static bool adjust_mover(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    prediction_player *player, int32_t to_time, qa_vec3 *out, qa_error *error)
{
    int32_t number;
    if (!ground_number(owner, player->view.ground, &number, error)) return false;
    return owner->options.adjust_mover(owner->options.context, source, qa_movement_origin(&player->view.movement),
        number, source->scene.physics_time, to_time, out, error);
}
static bool item_allowed(const qa_q3_player *p, const qa_q3_entity *entity,
    int32_t game_type, const qa_q3_item **out, bool *allowed, qa_error *error)
{
    size_t count;
    const qa_q3_item *items = qa_q3_items(p->product, &count);
    if (entity->modelindex < 1 || (size_t)entity->modelindex >= count)
        return fail(error, QA_ERROR_FORMAT, "BG_CanItemBeGrabbed: index out of range");
    const qa_q3_item *item = items + entity->modelindex;
    bool missionpack = p->product == QA_Q3_TEAM_ARENA;
    int32_t team = p->persistant[3], max_health = p->stats[stat_max_health(p->product)];
    int32_t persistent = missionpack ? item_tag(p->product, p->stats[2]) : 0;
    *out = item; *allowed = false;
    switch (item->kind) {
    case QA_Q3_ITEM_WEAPON: case QA_Q3_ITEM_POWERUP: *allowed = true; break;
    case QA_Q3_ITEM_AMMO:
        if (item->tag < 0 || item->tag >= 16) return fail(error, QA_ERROR_FORMAT, "Prediction ammo item has no source weapon slot");
        *allowed = p->ammo[item->tag] < 200; break;
    case QA_Q3_ITEM_ARMOR:
        *allowed = persistent != QA_Q3_P_SCOUT && (double)p->stats[missionpack ? 4 : 3] <
            (double)max_health * (persistent == QA_Q3_P_GUARD ? 1 : 2); break;
    case QA_Q3_ITEM_HEALTH:
        *allowed = (double)p->stats[0] < (double)max_health *
            (persistent != QA_Q3_P_GUARD && (item->quantity == 5 || item->quantity == 100) ? 2 : 1); break;
    case QA_Q3_ITEM_PERSISTENT:
        *allowed = missionpack && p->stats[2] == 0 && (!(entity->generic1 & 2) || team == 1) &&
            (!(entity->generic1 & 4) || team == 2); break;
    case QA_Q3_ITEM_TEAM:
        if (missionpack && game_type == 5) {
            *allowed = item->tag == QA_Q3_P_NEUTRALFLAG ||
                (team == 1 && item->tag == QA_Q3_P_BLUEFLAG && p->powerups[QA_Q3_P_NEUTRALFLAG]) ||
                (team == 2 && item->tag == QA_Q3_P_REDFLAG && p->powerups[QA_Q3_P_NEUTRALFLAG]);
            if (*allowed) break;
        }
        if (game_type == 4) {
            *allowed = (team == 1 && (item->tag == QA_Q3_P_BLUEFLAG ||
                (item->tag == QA_Q3_P_REDFLAG && (entity->modelindex2 || p->powerups[QA_Q3_P_BLUEFLAG])))) ||
                (team == 2 && (item->tag == QA_Q3_P_REDFLAG ||
                (item->tag == QA_Q3_P_BLUEFLAG && (entity->modelindex2 || p->powerups[QA_Q3_P_REDFLAG]))));
            break;
        }
        *allowed = missionpack && game_type == 7; break;
    case QA_Q3_ITEM_HOLDABLE: *allowed = p->stats[1] == 0; break;
    case QA_Q3_ITEM_BAD: return fail(error, QA_ERROR_FORMAT, "BG_CanItemBeGrabbed: IT_BAD");
    }
    return true;
}
static bool touch_item(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    prediction_state *state, const qa_q3_prediction_scene_entity_view *row, qa_error *error)
{
    if (!source->settings.predict_items) return true;
    qa_vec3 position;
    if (!owner->options.item_position(owner->options.context, source, row, &position, error)) return false;
    qa_vec3 delta = qa_vec_sub(qa_movement_origin(&state->predicted.view.movement), position);
    if (delta.x > 44 || delta.x < -50 || delta.y > 36 || delta.y < -36 || delta.z > 36 || delta.z < -36) return true;
    int32_t source_misc_time;
    if (!owner->options.item_misc_time(owner->options.context, source, row, &source_misc_time, error)) return false;
    prediction_item *claim = state->items + row->source_number;
    if (!claim->seen || claim->source_misc_time != source_misc_time) {
        claim->source_misc_time = source_misc_time; claim->misc_time = source_misc_time;
    }
    if (!claim->seen || claim->published != row->published || claim->publication_message != row->publication_message)
        claim->hidden = false;
    claim->seen = true; claim->published = row->published; claim->publication_message = row->publication_message;
    if (claim->misc_time == source->scene.time) return true;
    qa_q3_player *p = &state->predicted.view.player;
    const qa_q3_item *item; bool allowed;
    if (!item_allowed(p, row->entity, source->settings.game_type, &item, &allowed, error)) return false;
    if (!allowed || (p->product == QA_Q3_TEAM_ARENA && source->settings.game_type == 5 && item->tag != QA_Q3_P_NEUTRALFLAG)) return true;
    if (source->settings.game_type == 4 || (p->product == QA_Q3_TEAM_ARENA && source->settings.game_type == 7)) {
        if ((p->persistant[3] == 1 && item->tag == QA_Q3_P_REDFLAG) ||
            (p->persistant[3] == 2 && item->tag == QA_Q3_P_BLUEFLAG)) return true;
    }
    event_parameter(&state->predicted, 19, row->entity->modelindex);
    claim->hidden = true; claim->misc_time = source->scene.time;
    if (item->kind == QA_Q3_ITEM_WEAPON) {
        if (item->tag < 1 || item->tag >= 16) return fail(error, QA_ERROR_FORMAT, "Predicted item has no source weapon bit");
        p->stats[stat_weapons(p->product)] = signed_word((uint32_t)p->stats[stat_weapons(p->product)] | (1u << (uint32_t)item->tag));
        if (!p->ammo[item->tag]) p->ammo[item->tag] = 1;
    }
    return true;
}
static bool touch_triggers(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    prediction_state *state, qa_error *error)
{
    prediction_player *player = &state->predicted;
    qa_q3_player *p = &player->view.player;
    if (p->stats[0] <= 0 || (p->pmType != 0 && p->pmType != 2)) return true;
    size_t count;
    if (!owner->options.trigger_count(owner->options.context, source, &count, error)) return false;
    if (count > QA_Q3_ENTITIES) return fail(error, QA_ERROR_FORMAT, "Prediction trigger list exceeds actual source rows");
    for (size_t i = 0; i < count; ++i) {
        qa_q3_prediction_scene_entity_view row = {0}; bool present = false;
        if (!owner->options.trigger_at(owner->options.context, source, i, &row, &present, error)) return false;
        if (!present || !row.entity || row.source_number >= QA_Q3_ENTITIES)
            return fail(error, QA_ERROR_NOT_FOUND, "Prediction trigger lost its actual retained centity");
        if (row.entity->eType == 2 && p->pmType != 2) {
            if (!touch_item(owner, source, state, &row, error)) return false;
            continue;
        }
        if (row.entity->solid != 0xffffff || !row.entity->modelindex) continue;
        bool overlap = false;
        if (!owner->options.trigger_overlap(owner->options.context, source, &row,
                qa_movement_origin(&player->view.movement), player->view.bounds, &overlap, error)) return false;
        if (!overlap) continue;
        if (row.entity->eType == 9) player->view.hyperspace = true;
        else if (row.entity->eType == 8 && p->pmType == 0 && !p->powerups[QA_Q3_P_FLIGHT]) {
            qa_vec3 velocity = vector(row.entity->origin2);
            if (p->jumppadEnt != row.entity->number) {
                float pitch = velocity.x == 0 && velocity.y == 0 ? -90.0f :
                    -atan2f(velocity.z, hypotf(velocity.x, velocity.y)) * (180.0f / 3.14159265358979323846f);
                event_parameter(player, 13, fabsf(pitch) < 45 ? 0 : 1);
            }
            p->jumppadEnt = row.entity->number; p->jumppadFrame = p->pmoveFramecount;
            if (!qa_movement_set_velocity(&player->view.movement, velocity, error)) return false;
            if (player->view.movement.kind == QA_RULESET_Q3) {
                qa_actor_id actor = {0}; bool found = false;
                if (row.entity->number < 0 || row.entity->number >= QA_Q3_ENTITY_WORLD)
                    return fail(error, QA_ERROR_FORMAT, "Predicted jump pad has no source entity number");
                if (!owner->options.actor_at(owner->options.context, (uint32_t)row.entity->number, &actor, &found, error)) return false;
                if (!found) return fail(error, QA_ERROR_NOT_FOUND, "Predicted jump pad lacks its genuine source observer identity");
                player->view.movement.data.q3.jump_pad = actor;
                player->view.movement.data.q3.jump_pad_frame = player->view.movement.data.q3.movement_frame;
            }
        }
    }
    if (p->jumppadFrame != p->pmoveFramecount) {
        p->jumppadFrame = 0; p->jumppadEnt = 0;
        if (player->view.movement.kind == QA_RULESET_Q3) qa_movement_q3_finish_jump_pads(&player->view.movement);
    }
    return write_player(owner, player, error);
}
static bool step(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    prediction_player *player, prediction_command *entry, const prediction_command *selected,
    bool first, qa_vec3 *pml, qa_collision_bits trace_mask, int32_t captured_pmove_msec, qa_error *error)
{
    qa_movement_input input = source->configuration.input;
    input.state = player->view.movement;
    input.command = input.state.kind == QA_RULESET_Q3 ? entry->source : relative_command(selected, &input.state);
    input.environment = player->environment;
    input.current_bounds = player->view.bounds; input.has_current_bounds = true;
    input.prediction = true;
    if (input.state.kind == QA_RULESET_Q3) {
        if (source->settings.pmove_fixed && captured_pmove_msec < 1)
            return fail(error, QA_ERROR_ARGUMENT, "Prediction captured a nonpositive fixed movement subdivision");
        input.profile.data.q3.fixed_ms = source->settings.pmove_fixed ? (uint32_t)captured_pmove_msec : 0;
        input.profile.data.q3.no_footsteps = (source->settings.dm_flags & 32) != 0;
        input.trace_policy.contents_mask = trace_mask;
        input.trace_policy.curves = input.trace_policy.player_curve_clip = true;
        input.has_trace_policy = true;
        if (source->settings.pmove_fixed) {
            int32_t rounded = add_word(entry->source_time, captured_pmove_msec - 1);
            int32_t quotient = rounded / captured_pmove_msec;
            input.command.server_time_ms = signed_word((uint32_t)quotient * (uint32_t)captured_pmove_msec);
        }
    }
    input.view_offset = player->view.view_offset;
    input.q2r_pml_origin = pml;
    input.snap_initial = input.state.kind == QA_RULESET_Q2_RERELEASE && first;
    if (input.state.kind == QA_RULESET_Q2_CLASSIC) input.profile.data.q2.snap_initial = false;
    int64_t duration = input.state.kind == QA_RULESET_NETQUAKE || input.state.kind == QA_RULESET_Q3 ?
        (int64_t)entry->source_time - player->view.command_time : input.command.milliseconds;
    if (duration < 0) duration = 0;
    input.elapsed_ns = (uint64_t)duration * UINT64_C(1000000);
    input.time_ns = selected->receipt_time_ns;
    input.has_source_seconds = input.state.kind == QA_RULESET_NETQUAKE;
    input.source_seconds = (double)entry->source_time / 1000.0;
    /* The event receipt clock is independent of signed source physics time. */
    if (input.state.kind == QA_RULESET_NETQUAKE) {
        if (duration > UINT32_MAX) return fail(error, QA_ERROR_ARGUMENT, "Private NQ command interval exceeds native duration");
        input.command.milliseconds = (uint32_t)duration;
    }
    replay_context context = {owner, source, player, selected};
    qa_movement_services services = {.context = &context, .trace = trace, .point_contents = contents,
        .is_bsp = is_bsp, .touch = touch, .firing = firing, .phase = phase, .effect = effect};
    if (!source_current(&context, error) || !qa_movement_move(&input, &services, &owner->scratch, error)) return false;
    const qa_movement_result *result = &owner->scratch;
    if (result->status != QA_MOVEMENT_ACTIVE)
        return fail(error, QA_ERROR_ARGUMENT, "Private prediction cannot retire an authoritative player");
    if (source->configuration.q3_arsenal &&
        (input.state.kind == QA_RULESET_Q2_CLASSIC || input.state.kind == QA_RULESET_Q2_RERELEASE)) {
        uint32_t before = (uint32_t)player->view.player.eventSequence;
        if (!weapon(&context, &input.command, input.command.milliseconds, error)) return false;
        player->arsenal_event_sequence = signed_word((uint32_t)player->arsenal_event_sequence +
            ((uint32_t)player->view.player.eventSequence - before));
        if (player->requested_weapon == player->view.player.weapon) player->requested_weapon = -1;
    }
    player->view.movement = result->state; player->view.bounds = result->bounds;
    player->view.view_angles = result->view_angles;
    player->view.command_angles = input.command.kind == QA_RULESET_Q3 || input.command.kind == QA_RULESET_Q2_CLASSIC ?
        qa_v3(angle(input.command.angle_words[0]), angle(input.command.angle_words[1]), angle(input.command.angle_words[2])) : input.command.angles;
    player->view.view_offset = result->view_offset; player->view.view_height = result->view_height;
    player->view.ground = result->ground; player->view.water_level = result->water_level;
    player->view.water_type = result->water_type;
    player->view.command_time = input.state.kind == QA_RULESET_Q3 ? result->state.data.q3.command_time_ms : entry->source_time;
    player->view.sequence = entry->source_sequence;
    if (!write_player(owner, player, error) || !source_current(&context, error)) return false;
    entry->continuation = *player; entry->has_continuation = true;
    return true;
}

static bool source_valid(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source)
{
    const frontend_remote_input_source *input = &source->input;
    const qa_application_control_prediction_configuration *configuration = &source->configuration;
    const qa_q3_snapshot *selected = seed_snapshot(source);
    return input->connection.owner && input->connection.generation && input->epoch &&
        input->receiver.session == owner->options.session && input->receiver.receiver &&
        input->receiver.initialized && input->input_settings && input->frame.kind == QA_RULESET_Q3 &&
        configuration->movement && configuration->character && configuration->arsenal &&
        qa_actors_get(qa_session_actors(owner->options.session), configuration->input.actor) &&
        configuration->input.state.kind == configuration->input.profile.kind &&
        configuration->input.prediction && !configuration->input.q2r_pml_origin &&
        (!configuration->q3_character || configuration->native_q3_character) &&
        (!configuration->q3_arsenal || configuration->native_q3_arsenal) &&
        source->geometry && qa_collision_map_identity(source->geometry) && selected && source->scene.snapshot &&
        isfinite(source->settings.error_decay_value) &&
        (!source->settings.error_decay_integer || source->settings.error_decay_value != 0) &&
        (selected == source->scene.snapshot || selected == source->scene.next_snapshot) &&
        selected->player.clientNum == (int32_t)input->receiver.source_client &&
        selected->server_time == source->scene.physics_time &&
        owner->options.configuration_current(owner->options.context, configuration) &&
        owner->options.source_current(owner->options.context, source);
}
static bool same_identity(const prediction_state *state,
    const frontend_remote_prediction_source *source)
{
    const qa_application_control_prediction_configuration *c = &source->configuration;
    return qa_net_client_id_equal(state->connection, source->input.connection) &&
        state->epoch == source->input.epoch && state->restart_generation == source->restart_generation &&
        state->map_identity == qa_collision_map_identity(source->geometry) &&
        qa_actor_id_equal(state->actor, c->input.actor) && state->movement == c->movement &&
        state->character == c->character && state->arsenal == c->arsenal &&
        state->baseline.view.movement.kind == c->input.state.kind;
}
static void set_identity(prediction_state *state, const frontend_remote_prediction_source *source)
{
    state->connection = source->input.connection; state->epoch = source->input.epoch;
    state->restart_generation = source->restart_generation;
    state->map_identity = qa_collision_map_identity(source->geometry);
    state->actor = source->configuration.input.actor;
    state->movement = source->configuration.movement; state->character = source->configuration.character;
    state->arsenal = source->configuration.arsenal;
}
static void set_receipt(prediction_state *state, const frontend_remote_prediction_source *source)
{
    state->snapshot_number = seed_snapshot(source)->message_number;
    state->receiver = source->input.receiver.receiver;
    state->service_owner = source->input.receiver.service_owner;
    state->launch_seat = source->input.receiver.seat;
    state->source_client = source->input.receiver.source_client;
    state->receipt_time_ns = source->receipt_time_ns;
    state->presentation_time = source->scene.time;
    state->physics_time = source->scene.physics_time;
}
static bool same_receipt(const prediction_state *state, const frontend_remote_prediction_source *source)
{
    const qa_q3_snapshot *snapshot = seed_snapshot(source);
    return snapshot && same_identity(state, source) && state->snapshot_number == snapshot->message_number &&
        state->receiver == source->input.receiver.receiver && state->service_owner == source->input.receiver.service_owner &&
        state->launch_seat == source->input.receiver.seat && state->source_client == source->input.receiver.source_client &&
        state->receipt_time_ns == source->receipt_time_ns && state->presentation_time == source->scene.time &&
        state->physics_time == source->scene.physics_time;
}
static prediction_player initial_player(const qa_application_control_prediction_configuration *c)
{
    return (prediction_player){.view = {.movement = c->input.state,
        .bounds = c->input.current_bounds, .view_angles = c->view_angles,
        .command_angles = c->command_angles, .view_height = c->view_height, .ground = c->ground,
        .water_level = c->water_level, .water_type = c->water_type, .view_offset = c->input.view_offset},
        .environment = c->input.environment, .fractional_weapon_ms = c->fractional_weapon_ms,
        .external_slot = c->external_weapon_slot, .requested_weapon = c->requested_weapon};
}
static bool native_capabilities(const qa_application_control_prediction_configuration *c)
{
    return (!c->q3_character || c->native_q3_character) && (!c->q3_arsenal || c->native_q3_arsenal);
}
static bool warning(frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    const char *message, qa_error *error)
{
    if (!source->settings.show_miss) return true;
    if (!receipt_current(owner, source))
        return fail(error, QA_ERROR_NOT_FOUND, "Prediction diagnostic lost its genuine source owner");
    return owner->options.warning(owner->options.context, source, message, error) &&
        (receipt_current(owner, source) ||
            fail(error, QA_ERROR_NOT_FOUND, "Prediction diagnostic retired its genuine source owner"));
}
static bool clamp_pmove_msec(frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source, qa_error *error)
{
    int32_t value = source->settings.pmove_msec;
    int32_t clamped = value < 8 ? 8 : value > 33 ? 33 : value;
    if (clamped == value) return true;
    if (!receipt_current(owner, source))
        return fail(error, QA_ERROR_NOT_FOUND, "Prediction movement clamp lost its actual CLIENT registry");
    if (!owner->options.set_pmove_msec(owner->options.context, source, clamped, error)) return false;
    /* Publication changes the real registry, not the retained CGAME cache.
     * Its actual update owner advances that scalar at the next source boundary. */
    return receipt_current(owner, source) ||
        fail(error, QA_ERROR_NOT_FOUND, "Prediction movement clamp changed its source receipt");
}
bool frontend_remote_prediction_create(const frontend_remote_prediction_options *options,
    frontend_remote_prediction **out, qa_error *error)
{
    if (options && !native_capabilities(&options->initial_configuration))
        return fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 prediction requires its genuine acquired source movement/arsenal ABI");
    if (!options || !options->session || !options->configuration_current || !options->source_read || !options->source_current ||
        !options->trace || !options->point_contents || !options->is_bsp || !options->adjust_mover ||
        !options->trigger_count || !options->trigger_at || !options->trigger_overlap || !options->item_position || !options->item_misc_time ||
        !options->set_pmove_msec || !options->warning ||
        !options->actor_at || !options->number_of || !out || *out ||
        !options->initial_configuration.movement || !options->initial_configuration.character ||
        !options->initial_configuration.arsenal || options->initial_configuration.input.q2r_pml_origin ||
        !qa_actors_get(qa_session_actors(options->session), options->initial_configuration.input.actor) ||
        !options->configuration_current(options->context, &options->initial_configuration))
        return fail(error, QA_ERROR_ARGUMENT, "Private prediction requires its real source and collision owners");
    frontend_remote_prediction *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating private selected movement prediction");
    owner->options = *options;
    owner->initial = initial_player(&options->initial_configuration);
    *out = owner;
    return true;
}
void frontend_remote_prediction_destroy(frontend_remote_prediction *owner)
{
    if (!owner) return;
    qa_movement_result_free(&owner->scratch); free(owner);
}
bool frontend_remote_prediction_initialized(const frontend_remote_prediction *owner)
{
    return owner && owner->state.initialized;
}
void frontend_remote_prediction_clear(frontend_remote_prediction *owner)
{
    if (!owner || owner->busy) return;
    memset(&owner->state, 0, sizeof(owner->state));
}
bool frontend_remote_prediction_admit_initial(frontend_remote_prediction *owner,
    const qa_usercmd *command, qa_error *error)
{
    if (!owner || owner->busy || owner->state.initialized || !command || command->kind != QA_RULESET_Q3 ||
        !command->sequence || command->server_time_ms || command->forward_move != 0 || command->side_move != 0 ||
        command->up_move != 0 || command->buttons || command->weapon || command->impulse || command->milliseconds ||
        command->angle_words[0] || command->angle_words[1] || command->angle_words[2] ||
        command->server_frame || command->light_level || command->acknowledged_server_seconds != 0 ||
        command->angles.x != 0 || command->angles.y != 0 || command->angles.z != 0)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction initial admission needs the genuine zero source receipt");
    frontend_remote_prediction_source source = {0}; bool present = false;
    if (!owner->options.source_read(owner->options.context, &source, &present, error)) return false;
    if (!present || !source_valid(owner, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction initial admission lost its constructor source");
    prediction_state state = {0};
    const qa_application_control_prediction_configuration *c = &owner->options.initial_configuration;
    if (!qa_actor_id_equal(c->input.actor, source.configuration.input.actor) ||
        c->movement != source.configuration.movement || c->character != source.configuration.character ||
        c->arsenal != source.configuration.arsenal || c->input.state.kind != source.configuration.input.state.kind)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction constructor roles differ from its admitted source");
    /* This is the actual constructor cut, before any selected receipt. The
     * decoded source state overlays origin/velocity and source-owned fields. */
    if (!seed(owner, &source, &owner->initial, &state.baseline, error) ||
        !owner->options.source_current(owner->options.context, &source)) return false;
    state.baseline.view.sequence = command->sequence;
    state.predicted = state.baseline;
    state.q2r_pml_origin = c->q2r_pml_origin;
    set_receipt(&state, &source);
    set_identity(&state, &source); state.initialized = true;
    owner->state = state;
    return true;
}
static bool command_valid(const qa_usercmd *command)
{
    if (!command || command->kind > QA_RULESET_Q3 || command->kind < QA_RULESET_NETQUAKE ||
        !command->sequence || !qa_vec_finite(command->angles) || !isfinite(command->acknowledged_server_seconds) ||
        !isfinite(command->forward_move) || !isfinite(command->side_move) || !isfinite(command->up_move)) return false;
    if ((command->kind == QA_RULESET_Q2_CLASSIC || command->kind == QA_RULESET_Q2_RERELEASE) &&
        command->milliseconds > 255) return false;
    if (command->kind == QA_RULESET_Q3)
        return command->forward_move == truncf(command->forward_move) && fabsf(command->forward_move) <= 127 &&
            command->side_move == truncf(command->side_move) && fabsf(command->side_move) <= 127 &&
            command->up_move == truncf(command->up_move) && fabsf(command->up_move) <= 127;
    return true;
}
static const prediction_command *selected_receipt(const prediction_state *state, size_t index)
{
    const prediction_command *selected = state->commands + index;
    if (state->baseline.view.movement.kind == QA_RULESET_Q3) return selected;
    /* The foreign adapter's command map replaces the selected payload at an
     * existing source clock. The literal source ring still retains each row. */
    for (size_t i = index + 1; i < state->count; ++i)
        if (state->commands[i].source_time == selected->source_time)
            selected = state->commands + i;
    return selected;
}
bool frontend_remote_prediction_submit(frontend_remote_prediction *owner,
    const qa_usercmd *selected, frontend_remote_prediction_angle_space angle_space,
    const qa_usercmd *source_command, qa_error *error)
{
    if (!owner || owner->busy || !owner->state.initialized || !command_valid(selected) ||
        !command_valid(source_command) || source_command->kind != QA_RULESET_Q3 ||
        selected->kind != owner->state.baseline.view.movement.kind ||
        angle_space > FRONTEND_REMOTE_PREDICTION_ABSOLUTE || angle_space < FRONTEND_REMOTE_PREDICTION_SOURCE_RELATIVE)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction requires one real paired selected and source command");
    frontend_remote_prediction_source source = {0}; bool present = false;
    if (!owner->options.source_read(owner->options.context, &source, &present, error)) return false;
    if (!present || !source_valid(owner, &source) || !same_identity(&owner->state, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction command belongs to a different live source");
    prediction_state *state = &owner->state;
    uint64_t previous = state->count ? state->commands[state->count - 1].source_sequence : state->baseline.view.sequence;
    if (source_command->sequence <= previous) return true;
    if (state->count && selected->sequence <= state->commands[state->count - 1].selected.sequence)
        return fail(error, QA_ERROR_ARGUMENT, "Selected physical prediction receipts must retain their real order");
    if (state->count == 64) {
        state->discarded_sequence = state->commands[0].source_sequence; state->has_discarded = true;
        memmove(state->commands, state->commands + 1, 63 * sizeof(*state->commands)); --state->count;
    }
    prediction_command command = {.selected = *selected, .angle_space = angle_space,
        .source = *source_command,
        .source_sequence = source_command->sequence, .receipt_time_ns = source.receipt_time_ns,
        .source_time = source_command->server_time_ms,
        .source_buttons = source_command->buttons, .source_weapon = source_command->weapon};
    memcpy(state->commands + state->count++, &command, sizeof(command));
    return true;
}

bool frontend_remote_prediction_source_read(const frontend_remote_prediction *owner,
    frontend_remote_prediction_source *out, bool *present, qa_error *error)
{
    if (!owner || owner->busy || !out || !present)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction source read requires its idle genuine owner");
    frontend_remote_prediction_source source = {0}; bool admitted = false;
    if (!owner->options.source_read(owner->options.context, &source, &admitted, error)) return false;
    if (!admitted) { *present = false; return true; }
    if (!source_valid(owner, &source))
        return fail(error, QA_ERROR_NOT_FOUND, "Prediction source read lost its genuine source receipt");
    *out = source; *present = true; return true;
}
static bool replay(frontend_remote_prediction *owner,
    const frontend_remote_prediction_cgame_seed *retail,
    frontend_remote_prediction_view *out, bool *present, qa_error *error)
{
    if (!owner || !out || !present || owner->busy)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction replay needs its idle actual owner");
    frontend_remote_prediction_source source = {0}; bool admitted = false;
    if (!owner->options.source_read(owner->options.context, &source, &admitted, error)) return false;
    if (!admitted) { *present = false; return true; }
    if (!owner->state.initialized || !source_valid(owner, &source) || !same_identity(&owner->state, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction replay lost its admitted connection and selected owners");
    if (retail && !retail_seed_valid(owner, &source, retail, error))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction replay lost its actual CGAME retail seed");
    owner->busy = true;
    owner->active_seed = retail;
    prediction_state next = owner->state;
    const qa_q3_snapshot *snapshot = player_snapshot(owner, &source);
    const qa_q3_snapshot *current = current_snapshot(owner, &source);
    if (retail && !next.valid_pps) {
        if (!seed_player(owner, &current->player, &next.baseline, &next.predicted, error) ||
            !receipt_current(owner, &source)) {
            owner->active_seed = NULL; owner->busy = false;
            return false;
        }
        next.valid_pps = true;
    }
    prediction_player base = next.baseline;
    bool acknowledged = source.has_acknowledged_sequence && !source.history_unavailable;
    bool available = acknowledged && base.view.sequence <= source.acknowledged_sequence &&
        base.view.command_time == snapshot->player.commandTime;
    if (acknowledged) {
        for (size_t i = 0; i < next.count; ++i) {
            prediction_command *entry = next.commands + i;
            if (entry->source_sequence <= source.acknowledged_sequence && entry->has_continuation &&
                entry->continuation.view.command_time == snapshot->player.commandTime &&
                (!available || entry->source_sequence >= base.view.sequence)) {
                base = entry->continuation; available = true;
            }
        }
    }
    /* Repeated raw clocks can acknowledge a row that did not move. Foreign
     * state still needs a genuine retained continuation at that exact PS
     * clock. Q3 carries its entire motion in the decoded PS. */
    bool q3_motion = next.baseline.view.movement.kind == QA_RULESET_Q3;
    if (q3_motion) available = true;
    if (!available || (!q3_motion && next.has_discarded && next.discarded_sequence > source.acknowledged_sequence)) {
        next.predicted.view.status = FRONTEND_REMOTE_PREDICTION_HISTORY_EXHAUSTED;
        next.predicted.view.hyperspace = next.predicted.view.consumed_teleport = false;
        set_receipt(&next, &source);
        if (!receipt_current(owner, &source)) {
            owner->active_seed = NULL;
            owner->busy = false;
            return fail(error, QA_ERROR_NOT_FOUND, "Exhausted prediction history lost its actual current presentation receipt");
        }
        owner->state = next;
        owner->active_seed = NULL;
        owner->busy = false; *out = next.predicted.view; *present = true;
        return true;
    }
    prediction_player old = next.predicted;
    uint32_t source_trace_mask = UINT32_C(1) | UINT32_C(0x10000) | UINT32_C(0x2000000);
    if (old.view.player.pmType == 3 || current->player.persistant[3] == 3)
        source_trace_mask &= ~UINT32_C(0x2000000);
    qa_collision_bits trace_mask = qa_collision_contents_mask(source_trace_mask, QA_COLLISION_Q3);
    bool ok = seed(owner, &source, &base, &next.baseline, error);
    if (ok) {
        set_receipt(&next, &source);
        next.predicted = next.baseline;
        next.predicted.view.hyperspace = false;
        next.predicted.view.consumed_teleport = false;
        qa_movement_state *state = &next.predicted.view.movement;
        bool follow = (current->player.pmFlags & 4096) != 0;
        bool disabled = (state->kind == QA_RULESET_Q2_CLASSIC && (state->data.q2.flags & 64u)) ||
            (state->kind == QA_RULESET_Q2_RERELEASE && (state->data.q2r.flags & 64u));
        if (source.settings.demo_playback || follow || source.settings.no_predict || source.settings.synchronous_clients) {
            ok = interpolate(owner, &source, &next, !source.settings.demo_playback && !follow, error);
        } else if (disabled) {
            if (next.count) ok = update_angles(&next.predicted, next.commands + next.count - 1, error);
            next.predicted.view.status = FRONTEND_REMOTE_PREDICTION_DISABLED;
            if (ok) ok = write_player(owner, &next.predicted, error);
        } else if (next.count == 64 && next.commands[0].source_time > current->player.commandTime &&
            next.commands[0].source_time < source.scene.time) {
            ok = warning(owner, &source, "exceeded PACKET_BACKUP on commands\n", error);
            next.predicted = old;
            next.predicted.view.hyperspace = false;
            next.predicted.view.consumed_teleport = false;
            next.predicted.view.status = FRONTEND_REMOTE_PREDICTION_HISTORY_EXHAUSTED;
        } else {
            bool moved = false;
            int32_t captured_pmove_msec = source.settings.pmove_msec;
            ok = clamp_pmove_msec(owner, &source, error);
            int32_t latest_time = next.count ? next.commands[next.count - 1].source_time : 0;
            int32_t last_time = 0;
            for (size_t i = 0; ok && i < next.count; ++i) {
                prediction_command *entry = next.commands + i;
                const prediction_command *selected = selected_receipt(&next, i);
                last_time = entry->source_time;
                if (source.settings.pmove_fixed) ok = update_angles(&next.predicted, selected, error);
                if (!ok || entry->source_time <= next.predicted.view.command_time || entry->source_time > latest_time) continue;
                if (next.predicted.view.command_time == old.view.command_time) {
                    if (this_teleport(owner, &source) && !next.predicted.view.consumed_teleport) {
                        next.prediction_error = qa_v3(0, 0, 0);
                        next.predicted.view.consumed_teleport = true;
                        ok = warning(owner, &source, "PredictionTeleport\n", error);
                    } else {
                        qa_vec3 adjusted;
                        ok = adjust_mover(owner, &source, &next.predicted, source.previous_presentation_time, &adjusted, error);
                        if (!ok) break;
                        qa_vec3 delta = qa_vec_sub(qa_movement_origin(&old.view.movement), adjusted);
                        if (delta.x != 0 || delta.y != 0 || delta.z != 0) ok = warning(owner, &source, "prediction error\n", error);
                        float length = qa_vec_length(delta);
                        if (ok && length > 0.1f) {
                            if (source.settings.show_miss) {
                                char message[96]; snprintf(message, sizeof(message), "Prediction miss: %.6f\n", (double)length);
                                ok = warning(owner, &source, message, error);
                            }
                            if (source.settings.error_decay_integer) {
                                float fraction = (source.settings.error_decay_value -
                                    (float)subtract_word(source.scene.time, next.prediction_error_time)) / source.settings.error_decay_value;
                                next.prediction_error = qa_vec_scale(next.prediction_error, fmaxf(0, fraction));
                                if (ok && fraction > 0 && source.settings.show_miss) {
                                    char message[96]; snprintf(message, sizeof(message), "Double prediction decay: %.6f\n", (double)fraction);
                                    ok = warning(owner, &source, message, error);
                                }
                            } else next.prediction_error = qa_v3(0, 0, 0);
                            next.prediction_error = qa_vec_add(delta, next.prediction_error);
                            next.prediction_error_time = source.previous_presentation_time;
                        }
                    }
                }
                if (!ok) break;
                ok = step(owner, &source, &next.predicted, entry, selected, !moved, &next.q2r_pml_origin,
                    trace_mask, captured_pmove_msec, error);
                if (ok) ok = touch_triggers(owner, &source, &next, error);
                if (ok) { entry->continuation = next.predicted; entry->has_continuation = true; moved = true; }
                if (ok && state->kind == QA_RULESET_Q3 && source.settings.pmove_fixed)
                    last_time = next.predicted.view.command_time;
            }
            if (ok && source.settings.show_miss > 1) {
                char message[96]; snprintf(message, sizeof(message), "[%d : %d] ", last_time, source.scene.time);
                ok = warning(owner, &source, message, error);
            }
            if (ok && !moved) ok = warning(owner, &source, "not moved\n", error);
            if (ok && moved) {
                qa_vec3 adjusted;
                ok = adjust_mover(owner, &source, &next.predicted, source.scene.time, &adjusted, error) &&
                    qa_movement_set_origin(&next.predicted.view.movement, adjusted, error) &&
                    write_player(owner, &next.predicted, error);
            }
            if (ok && moved && next.predicted.view.player.eventSequence > add_word(old.view.player.eventSequence, 2))
                ok = warning(owner, &source, "WARNING: dropped event\n", error);
            next.predicted.view.status = moved ? FRONTEND_REMOTE_PREDICTION_PREDICTED : FRONTEND_REMOTE_PREDICTION_UNCHANGED;
        }
        next.predicted.view.prediction_error = next.prediction_error;
        next.predicted.view.prediction_error_time = next.prediction_error_time;
    }
    if (ok) ok = receipt_current(owner, &source) ||
        fail(error, QA_ERROR_NOT_FOUND, "Prediction source retired before copied continuation publication");
    if (ok) { owner->state = next; *out = next.predicted.view; *present = true; }
    owner->active_seed = NULL;
    owner->busy = false;
    return ok;
}
bool frontend_remote_prediction_replay(frontend_remote_prediction *owner,
    frontend_remote_prediction_view *out, bool *present, qa_error *error)
{ return replay(owner, NULL, out, present, error); }
bool frontend_remote_prediction_replay_seed(frontend_remote_prediction *owner,
    const frontend_remote_prediction_cgame_seed *seed,
    frontend_remote_prediction_view *out, bool *present, qa_error *error)
{
    if (!seed) return fail(error, QA_ERROR_ARGUMENT, "CGAME prediction replay requires its actual retail seed");
    return replay(owner, seed, out, present, error);
}
bool frontend_remote_prediction_read(const frontend_remote_prediction *owner, const frontend_remote_prediction_source *source,
    frontend_remote_prediction_view *out)
{
    if (!owner || !source || !out || owner->busy || !owner->state.initialized ||
        !same_receipt(&owner->state, source) || !owner->options.source_current(owner->options.context, source)) return false;
    *out = owner->state.predicted.view;
    return true;
}
bool frontend_remote_prediction_error_clear(frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source, qa_error *error)
{
    if (!owner || owner->busy || !owner->state.initialized || !source ||
        !owner->options.source_current(owner->options.context, source) || !same_receipt(&owner->state, source))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction error feedback requires its completed current receipt");
    owner->state.prediction_error_time = 0;
    owner->state.predicted.view.prediction_error_time = 0;
    return true;
}
bool frontend_remote_prediction_entity_event_publish(frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source, int32_t before, int32_t after, qa_error *error)
{
    if (!owner || owner->busy || !owner->state.initialized || !source ||
        !owner->options.source_current(owner->options.context, source) || !same_receipt(&owner->state, source))
        return fail(error, QA_ERROR_ARGUMENT, "Prediction event feedback requires its completed current receipt");
    qa_q3_player *player = &owner->state.predicted.view.player;
    if (before != player->entityEventSequence)
        return fail(error, QA_ERROR_ARGUMENT, "Packet conversion changed its predicted player cursor");
    int32_t converted = before;
    if (!player->externalEvent && converted < player->eventSequence) {
        int32_t oldest = subtract_word(player->eventSequence, 2);
        if (converted < oldest) converted = oldest;
        converted = add_word(converted, 1);
    }
    if (after != converted)
        return fail(error, QA_ERROR_ARGUMENT, "Packet event cursor differs from its actual BG conversion");
    player->entityEventSequence = after;
    return true;
}
bool frontend_remote_prediction_item_read(const frontend_remote_prediction *owner,
    const frontend_remote_prediction_source *source, const qa_q3_prediction_scene_entity_view *row,
    int32_t *flags, int32_t *misc_time, qa_error *error)
{
    if (!owner || owner->busy || !owner->state.initialized || !source || !row || !row->entity ||
        row->source_number >= QA_Q3_ENTITIES || !flags || !misc_time ||
        !same_receipt(&owner->state, source) || !owner->options.source_current(owner->options.context, source))
        return fail(error, QA_ERROR_ARGUMENT, "Predicted item view requires its current presentation and centity owners");
    int32_t actual_misc_time;
    if (!owner->options.item_misc_time(owner->options.context, source, row, &actual_misc_time, error)) return false;
    const prediction_item *claim = owner->state.items + row->source_number;
    bool same_row = claim->seen && claim->published == row->published && claim->publication_message == row->publication_message;
    *flags = row->entity->eFlags;
    if (same_row && claim->hidden) *flags = signed_word((uint32_t)*flags | 0x80u);
    *misc_time = claim->seen && claim->source_misc_time == actual_misc_time ? claim->misc_time : actual_misc_time;
    return owner->options.source_current(owner->options.context, source) ||
        fail(error, QA_ERROR_NOT_FOUND, "Prediction item presentation retired during its pure read");
}

static bool environment_fields(qa_source_save_io *io, qa_movement_environment *v)
{
    uint32_t mode = v->mode;
    if (!qa_source_save_f32(io, &v->health) || !qa_source_save_bool(io, &v->flight) ||
        !qa_source_save_bool(io, &v->haste) || !qa_source_save_bool(io, &v->invulnerable) ||
        !qa_source_save_f32(io, &v->gravity_multiplier) || !qa_source_save_f32(io, &v->speed_multiplier) ||
        !qa_source_save_bool(io, &v->fixed_pose) || !qa_source_save_bool(io, &v->fixed_crouched) ||
        !qa_persistence_bounds(io, &v->pose.bounds) || !qa_source_save_f32(io, &v->pose.view_height) ||
        !qa_source_save_bool(io, &v->has_body_bounds) || !qa_persistence_bounds(io, &v->body_bounds) ||
        !qa_source_save_bool(io, &v->has_mode) || !qa_source_save_u32(io, &mode) ||
        mode > QA_MOVEMENT_MODE_FREEZE || !qa_source_save_bool(io, &v->has_stance) ||
        !qa_source_save_bool(io, &v->crouched)) return false;
    v->mode = (qa_movement_mode)mode;
    return isfinite(v->health) && isfinite(v->gravity_multiplier) && v->gravity_multiplier >= 0 &&
        isfinite(v->speed_multiplier) && v->speed_multiplier >= 0 && isfinite(v->pose.view_height);
}
static bool command_fields(qa_source_save_io *io, qa_usercmd *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_RULESET_Q3 ||
        !qa_source_save_u64(io, &v->sequence) || !qa_source_save_u32(io, &v->milliseconds) ||
        !qa_source_save_i32(io, &v->server_time_ms) || !qa_source_save_i32(io, &v->server_frame) ||
        !qa_source_save_f64(io, &v->acknowledged_server_seconds) || !qa_source_save_vec3(io, &v->angles)) return false;
    for (size_t i = 0; i < 3; ++i) if (!qa_source_save_i32(io, &v->angle_words[i])) return false;
    if (!qa_source_save_f32(io, &v->forward_move) || !qa_source_save_f32(io, &v->side_move) ||
        !qa_source_save_f32(io, &v->up_move) || !qa_source_save_u32(io, &v->buttons) ||
        !qa_source_save_u8(io, &v->weapon) || !qa_source_save_u8(io, &v->impulse) ||
        !qa_source_save_u8(io, &v->light_level)) return false;
    v->kind = (qa_ruleset_id)kind;
    return qa_vec_finite(v->angles) && isfinite(v->acknowledged_server_seconds) &&
        isfinite(v->forward_move) && isfinite(v->side_move) && isfinite(v->up_move);
}
static bool bounds_valid(qa_bounds value)
{
    return qa_vec_finite(value.mins) && qa_vec_finite(value.maxs) &&
        value.mins.x <= value.maxs.x && value.mins.y <= value.maxs.y && value.mins.z <= value.maxs.z;
}
static bool posture_fields(qa_source_save_io *io, qa_movement_posture *v)
{
    return qa_persistence_bounds(io, &v->bounds) && qa_source_save_f32(io, &v->view_height) &&
        bounds_valid(v->bounds) && isfinite(v->view_height);
}
static bool configuration_fields(qa_source_save_io *io, qa_application_control_prediction_configuration *v)
{
    qa_movement_input *in = &v->input;
    uint32_t shape = in->shape.kind, solid = in->q1_solid;
    uint32_t family = in->trace_policy.family, move = in->trace_policy.q1_move;
    uint32_t clock = v->clock.kind, rounding = v->numeric.rounding;
    uint32_t prediction_rounding = v->prediction_numeric.rounding;
    if (in->q2r_pml_origin || !qa_source_save_string(io, &v->movement) ||
        !qa_source_save_string(io, &v->character) || !qa_source_save_string(io, &v->arsenal) ||
        !qa_source_save_string(io, &v->profile_id) || !qa_source_save_u32(io, &clock) || clock > QA_RULESET_Q3 ||
        !qa_source_save_u64(io, &v->clock.initial_time_ns) || !qa_source_save_u64(io, &v->clock.interval_ns) ||
        !qa_source_save_u64(io, &v->clock.minimum_frame_ns) || !qa_source_save_u64(io, &v->clock.maximum_frame_ns) ||
        !qa_source_save_u64(io, &v->clock.initial_lead_ns) || !qa_source_save_u32(io, &v->clock.maximum_steps) ||
        !qa_source_save_string(io, &v->numeric.id) || !qa_source_save_bool(io, &v->numeric.native_c) ||
        !qa_source_save_u32(io, &v->numeric.radix) || !qa_source_save_u32(io, &v->numeric.scalar_mantissa_bits) ||
        !qa_source_save_u32(io, &v->numeric.double_mantissa_bits) ||
        !qa_source_save_i32(io, &v->numeric.evaluation_method) || !qa_source_save_u32(io, &rounding) ||
        rounding > QA_APPLICATION_ROUND_ZERO || !qa_source_save_bool(io, &v->numeric.qw_origin_binary64) ||
        !qa_source_save_string(io, &v->prediction_numeric.id) || !qa_source_save_bool(io, &v->prediction_numeric.native_c) ||
        !qa_source_save_u32(io, &v->prediction_numeric.radix) || !qa_source_save_u32(io, &v->prediction_numeric.scalar_mantissa_bits) ||
        !qa_source_save_u32(io, &v->prediction_numeric.double_mantissa_bits) ||
        !qa_source_save_i32(io, &v->prediction_numeric.evaluation_method) || !qa_source_save_u32(io, &prediction_rounding) ||
        prediction_rounding > QA_APPLICATION_ROUND_ZERO || !qa_source_save_bool(io, &v->prediction_numeric.qw_origin_binary64) ||
        !qa_source_save_actor(io, &in->actor) || !qa_persistence_movement(io, &in->state) ||
        !command_fields(io, &in->command) || !qa_persistence_movement_profile(io, &in->profile) ||
        !qa_source_save_u32(io, &shape) || shape > QA_SHAPE_CAPSULE ||
        !qa_persistence_bounds(io, &in->shape.bounds) || !qa_persistence_bounds(io, &in->current_bounds) ||
        !qa_source_save_bool(io, &in->has_current_bounds) || !posture_fields(io, &in->standing) ||
        !posture_fields(io, &in->crouched) || !posture_fields(io, &in->dead) ||
        !qa_persistence_bounds(io, &in->invulnerability_bounds) || !environment_fields(io, &in->environment) ||
        !qa_source_save_u64(io, &in->time_ns) || !qa_source_save_u64(io, &in->elapsed_ns) ||
        !qa_source_save_bool(io, &in->has_source_seconds) || !qa_source_save_f64(io, &in->source_seconds) ||
        !qa_source_save_bool(io, &in->prediction) || !qa_source_save_bool(io, &in->snap_initial) ||
        !qa_source_save_vec3(io, &in->view_offset) || !qa_source_save_bool(io, &in->has_source_punch_angles) ||
        !qa_source_save_vec3(io, &in->source_punch_angles) || !qa_source_save_u32(io, &solid) ||
        solid > QA_Q1_SOLID_CORPSE || !qa_source_save_u32(io, &family) || family < QA_COLLISION_Q1 ||
        family > QA_COLLISION_Q3 || !qa_source_save_u64(io, &in->trace_policy.contents_mask.lo) ||
        !qa_source_save_u64(io, &in->trace_policy.contents_mask.hi) ||
        !qa_source_save_u32(io, &move) || move > QA_Q1_MOVE_MISSILE ||
        !qa_source_save_i32(io, &in->trace_policy.q1_hull) ||
        !qa_source_save_bool(io, &in->trace_policy.q2_merged_contents) ||
        !qa_source_save_bool(io, &in->trace_policy.curves) ||
        !qa_source_save_bool(io, &in->trace_policy.player_curve_clip) ||
        !qa_source_save_bool(io, &in->has_trace_policy) ||
        !qa_source_save_vec3(io, &v->q2r_pml_origin) || !qa_source_save_vec3(io, &v->view_angles) ||
        !qa_source_save_vec3(io, &v->command_angles) || !qa_persistence_ground(io, &v->ground) ||
        !qa_source_save_f32(io, &v->view_height) || !qa_source_save_i32(io, &v->water_level) ||
        !qa_source_save_i32(io, &v->water_type) || !qa_source_save_bool(io, &v->q3_character) ||
        !qa_source_save_bool(io, &v->q3_arsenal) || !qa_source_save_bool(io, &v->native_q3_character) ||
        !qa_source_save_bool(io, &v->native_q3_arsenal) || !qa_source_save_f32(io, &v->fractional_weapon_ms) ||
        !qa_source_save_u32(io, &v->external_weapon_slot) || !qa_source_save_i32(io, &v->requested_weapon) ||
        !qa_source_save_bool(io, &v->has_client_view_offset) || !qa_source_save_vec3(io, &v->client_view_offset)) return false;
    v->clock.kind = (qa_ruleset_id)clock;
    v->numeric.rounding = (qa_application_numeric_rounding)rounding;
    v->prediction_numeric.rounding = (qa_application_numeric_rounding)prediction_rounding;
    in->shape.kind = (qa_shape_kind)shape; in->q1_solid = (qa_q1_solid)solid;
    in->trace_policy.family = (qa_collision_family)family;
    in->trace_policy.q1_move = (qa_q1_move_kind)move;
    return v->movement && v->character && v->arsenal && v->profile_id == v->movement &&
        v->prediction_numeric.native_c && v->prediction_numeric.id &&
        v->prediction_numeric.radix && v->prediction_numeric.scalar_mantissa_bits &&
        v->prediction_numeric.double_mantissa_bits && v->prediction_numeric.qw_origin_binary64 == (in->state.kind == QA_RULESET_QUAKEWORLD) &&
        in->actor.registry && in->prediction && qa_vec_finite(v->client_view_offset) &&
        (v->has_client_view_offset || (v->client_view_offset.x == 0 && v->client_view_offset.y == 0 && v->client_view_offset.z == 0)) &&
        (v->numeric.native_c ? v->numeric.id && v->numeric.radix && v->numeric.scalar_mantissa_bits &&
            v->numeric.double_mantissa_bits && v->numeric.qw_origin_binary64 == (in->state.kind == QA_RULESET_QUAKEWORLD) :
            !v->numeric.id && !v->numeric.radix && !v->numeric.scalar_mantissa_bits &&
            !v->numeric.double_mantissa_bits && !v->numeric.evaluation_method &&
            !v->numeric.rounding && !v->numeric.qw_origin_binary64) &&
        in->state.kind == in->profile.kind && in->command.kind == in->state.kind &&
        (!in->has_source_seconds || isfinite(in->source_seconds)) &&
        bounds_valid(in->shape.bounds) && bounds_valid(in->current_bounds) &&
        bounds_valid(in->invulnerability_bounds) &&
        (!in->environment.has_body_bounds || bounds_valid(in->environment.body_bounds)) &&
        (!in->environment.fixed_pose || bounds_valid(in->environment.pose.bounds)) &&
        qa_vec_finite(v->q2r_pml_origin) && qa_vec_finite(v->view_angles) && qa_vec_finite(v->command_angles) &&
        qa_vec_finite(in->view_offset) && qa_vec_finite(in->source_punch_angles) && isfinite(v->view_height) &&
        v->water_level >= 0 && v->water_level <= 3 && native_capabilities(v) &&
        (!v->native_q3_character || v->q3_character) && (!v->native_q3_arsenal || v->q3_arsenal) &&
        isfinite(v->fractional_weapon_ms) && v->fractional_weapon_ms >= 0 && v->fractional_weapon_ms < 1 &&
        v->external_weapon_slot <= 4 && v->requested_weapon >= -1 && v->requested_weapon < QA_Q3_WEAPON_COUNT &&
        (v->native_q3_arsenal || (v->fractional_weapon_ms == 0 && !v->external_weapon_slot && v->requested_weapon == -1));
}
static bool player_fields(qa_source_save_io *io, prediction_player *v)
{
    frontend_remote_prediction_view *view = &v->view;
    uint8_t encoded[1024];
    size_t size = 0;
    uint32_t product = view->player.product, status = view->status;
    if (product > QA_Q3_TEAM_ARENA) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        qa_net_writer writer; qa_net_writer_init(&writer, encoded, sizeof(encoded), io->error);
        if (!qa_q3_save_player_fields(&writer, &view->player)) return false;
        size = qa_net_writer_size(&writer);
    }
    if (!qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA ||
        !qa_source_save_count(io, &size, sizeof(encoded)) || !qa_source_save_bytes(io, encoded, size)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        qa_net_reader reader; qa_net_reader_init(&reader, (qa_bytes){encoded, size}, io->error);
        if (!qa_q3_restore_player_fields(&reader, &view->player, (qa_q3_product)product) ||
            reader.failed || reader.bit != size * 8) return false;
    }
    if (!qa_persistence_movement(io, &view->movement) || !qa_persistence_bounds(io, &view->bounds) ||
        !qa_source_save_vec3(io, &view->view_angles) || !qa_source_save_vec3(io, &view->command_angles) ||
        !qa_source_save_vec3(io, &view->view_offset) || !qa_persistence_ground(io, &view->ground) ||
        !qa_source_save_f32(io, &view->view_height) || !qa_source_save_i32(io, &view->water_level) ||
        !qa_source_save_i32(io, &view->water_type) || !qa_source_save_i32(io, &view->command_time) ||
        !qa_source_save_vec3(io, &view->prediction_error) || !qa_source_save_i32(io, &view->prediction_error_time) ||
        !qa_source_save_bool(io, &view->hyperspace) || !qa_source_save_bool(io, &view->consumed_teleport) ||
        !qa_source_save_u64(io, &view->sequence) || !qa_source_save_u32(io, &status) ||
        status > FRONTEND_REMOTE_PREDICTION_HISTORY_EXHAUSTED || !environment_fields(io, &v->environment) ||
        !qa_source_save_f32(io, &v->fractional_weapon_ms) || !qa_source_save_i32(io, &v->arsenal_event_sequence) ||
        !qa_source_save_u32(io, &v->external_slot) || !qa_source_save_i32(io, &v->requested_weapon)) return false;
    view->status = (frontend_remote_prediction_status)status;
    return bounds_valid(view->bounds) && qa_vec_finite(qa_movement_origin(&view->movement)) &&
        qa_vec_finite(qa_movement_velocity(&view->movement)) &&
        qa_vec_finite(view->view_angles) && qa_vec_finite(view->command_angles) &&
        qa_vec_finite(view->view_offset) && isfinite(view->view_height) &&
        qa_vec_finite(view->prediction_error) &&
        view->water_level >= 0 && view->water_level <= 3 && isfinite(v->fractional_weapon_ms) &&
        v->fractional_weapon_ms >= 0 && v->fractional_weapon_ms < 1 && v->external_slot <= 4 &&
        v->requested_weapon >= -1 && v->requested_weapon < QA_Q3_WEAPON_COUNT;
}
static bool state_fields(qa_source_save_io *io, prediction_state *state,
    qa_application_control_prediction_configuration *configuration, prediction_player *initial)
{
    uint8_t magic[4] = {'Q','R','P','D'};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QRPD", sizeof(magic)) ||
        !configuration_fields(io, configuration) ||
        !qa_source_save_bool(io, &state->initialized) ||
        !qa_source_save_bool(io, &state->valid_pps) || (state->valid_pps && !state->initialized)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *initial = initial_player(configuration);
    if (!state->initialized) return true;
    if (!qa_source_save_u64(io, &state->connection.owner) || !qa_source_save_u32(io, &state->connection.slot) ||
        !qa_source_save_u64(io, &state->connection.generation) || !qa_source_save_actor(io, &state->actor) ||
        !qa_source_save_string(io, &state->movement) || !qa_source_save_string(io, &state->character) ||
        !qa_source_save_string(io, &state->arsenal) || !qa_source_save_u64(io, &state->epoch) ||
        !qa_source_save_u64(io, &state->map_identity) || !qa_source_save_u64(io, &state->restart_generation) ||
        !qa_source_save_i32(io, &state->snapshot_number) || !qa_source_save_vec3(io, &state->q2r_pml_origin) ||
        !qa_source_save_string(io, &state->receiver) || !qa_source_save_u64(io, &state->service_owner) ||
        !qa_source_save_u32(io, &state->launch_seat) || !qa_source_save_u32(io, &state->source_client) ||
        !qa_source_save_u64(io, &state->receipt_time_ns) || !qa_source_save_i32(io, &state->presentation_time) ||
        !qa_source_save_i32(io, &state->physics_time) ||
        !qa_source_save_vec3(io, &state->prediction_error) || !qa_source_save_i32(io, &state->prediction_error_time) ||
        !player_fields(io, &state->baseline) || !player_fields(io, &state->predicted) ||
        !qa_source_save_bool(io, &state->has_discarded) || !qa_source_save_u64(io, &state->discarded_sequence) ||
        !qa_source_save_count(io, &state->count, 64)) return false;
    qa_ruleset_id kind = configuration->input.state.kind;
    if (!state->connection.owner || !state->connection.generation || !state->epoch || !state->map_identity ||
        !state->receiver || !state->service_owner || state->source_client >= 64 ||
        !qa_actor_id_equal(state->actor, configuration->input.actor) || state->movement != configuration->movement ||
        state->character != configuration->character || state->arsenal != configuration->arsenal ||
        !qa_vec_finite(state->q2r_pml_origin) || state->baseline.view.movement.kind != kind ||
        state->predicted.view.movement.kind != kind || !state->baseline.view.sequence ||
        !qa_vec_finite(state->prediction_error) ||
        (!state->has_discarded && state->discarded_sequence) ||
        (state->has_discarded && (!state->discarded_sequence || state->count != 64))) return false;
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i) {
        prediction_item *item = state->items + i;
        if (!qa_source_save_bool(io, &item->seen)) return false;
        if (!item->seen) {
            if (item->published || item->hidden || item->publication_message || item->source_misc_time || item->misc_time) return false;
            continue;
        }
        if (!qa_source_save_bool(io, &item->published) || !qa_source_save_i32(io, &item->publication_message) ||
            (!item->published && item->publication_message) || !qa_source_save_i32(io, &item->source_misc_time) ||
            !qa_source_save_i32(io, &item->misc_time) || !qa_source_save_bool(io, &item->hidden)) return false;
    }
    uint64_t prior_source = 0, prior_selected = 0;
    for (size_t i = 0; i < state->count; ++i) {
        prediction_command *entry = state->commands + i;
        uint32_t space = entry->angle_space;
        if (!command_fields(io, &entry->selected) || !command_valid(&entry->selected) ||
            !command_fields(io, &entry->source) || !command_valid(&entry->source) || entry->source.kind != QA_RULESET_Q3 ||
            entry->selected.kind != kind || !qa_source_save_u32(io, &space) ||
            space > FRONTEND_REMOTE_PREDICTION_ABSOLUTE || !qa_source_save_u64(io, &entry->source_sequence) ||
            !qa_source_save_u64(io, &entry->receipt_time_ns) || !qa_source_save_i32(io, &entry->source_time) ||
            !qa_source_save_u32(io, &entry->source_buttons) || !qa_source_save_u8(io, &entry->source_weapon) ||
            !qa_source_save_bool(io, &entry->has_continuation) ||
            (entry->has_continuation && !player_fields(io, &entry->continuation)) ||
            entry->source_sequence <= prior_source || (i && entry->selected.sequence <= prior_selected) ||
            entry->source.sequence != entry->source_sequence || entry->source.server_time_ms != entry->source_time ||
            entry->source.buttons != entry->source_buttons || entry->source.weapon != entry->source_weapon ||
            (entry->has_continuation && (entry->continuation.view.movement.kind != kind ||
                entry->continuation.view.sequence != entry->source_sequence ||
                (kind == QA_RULESET_Q3 ? entry->continuation.view.command_time != entry->continuation.view.movement.data.q3.command_time_ms :
                    entry->continuation.view.command_time != entry->source_time)))) return false;
        entry->angle_space = (frontend_remote_prediction_angle_space)space;
        prior_source = entry->source_sequence; prior_selected = entry->selected.sequence;
    }
    return !state->count || state->predicted.view.sequence <= prior_source;
}
bool frontend_remote_prediction_checkpoint(const frontend_remote_prediction *owner,
    qa_buffer *out, qa_error *error)
{
    if (!owner || owner->busy || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction capture requires its idle copied continuation");
    prediction_state state = owner->state;
    prediction_player initial = owner->initial;
    qa_application_control_prediction_configuration configuration = owner->options.initial_configuration;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, owner->options.session, error) &&
        state_fields(&io, &state, &configuration, &initial) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "Invalid private prediction continuation");
    return ok;
}
bool frontend_remote_prediction_restore(frontend_remote_prediction *owner, qa_bytes bytes, qa_error *error)
{
    if (!owner || owner->busy) return fail(error, QA_ERROR_ARGUMENT, "Prediction restore needs its idle actual candidate");
    prediction_state state = {0}; prediction_player initial = {0};
    qa_application_control_prediction_configuration configuration = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, owner->options.session, bytes, error) &&
        state_fields(&io, &state, &configuration, &initial) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    frontend_remote_prediction_source source = {0}; bool present = false;
    if (ok) ok = owner->options.configuration_current(owner->options.context, &configuration);
    if (ok && state.initialized) ok = owner->options.source_read(owner->options.context, &source, &present, error);
    if (ok && state.initialized) ok = present && source_valid(owner, &source) &&
        qa_actor_id_equal(configuration.input.actor, source.configuration.input.actor) &&
        configuration.movement == source.configuration.movement && configuration.character == source.configuration.character &&
        configuration.arsenal == source.configuration.arsenal &&
        configuration.input.state.kind == source.configuration.input.state.kind &&
        (!state.initialized || same_identity(&state, &source)) &&
        owner->options.source_current(owner->options.context, &source);
    if (!ok) {
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Saved prediction differs from its genuine candidate source");
        return false;
    }
    owner->options.initial_configuration = configuration;
    owner->initial = initial; owner->state = state;
    return true;
}
bool frontend_remote_prediction_restore_new(const frontend_remote_prediction_options *bindings,
    qa_bytes bytes, frontend_remote_prediction **out, qa_error *error)
{
    if (!bindings || !bindings->session || !bindings->configuration_current || !bindings->source_read ||
        !bindings->source_current || !bindings->actor_at || !bindings->number_of || !bindings->trace ||
        !bindings->point_contents || !bindings->is_bsp || !bindings->adjust_mover || !bindings->trigger_count ||
        !bindings->trigger_at || !bindings->trigger_overlap || !bindings->item_position || !bindings->item_misc_time ||
        !bindings->set_pmove_msec || !bindings->warning || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Prediction restore needs actual candidate bindings and an empty holder");
    prediction_state state = {0}; prediction_player initial = {0};
    qa_application_control_prediction_configuration configuration = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, bindings->session, bytes, error) &&
        state_fields(&io, &state, &configuration, &initial) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) ok = qa_actors_get(qa_session_actors(bindings->session), configuration.input.actor) &&
        bindings->configuration_current(bindings->context, &configuration);
    if (!ok) {
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid saved prediction constructor and continuation");
        return false;
    }
    frontend_remote_prediction *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating actual restored private prediction owner");
    owner->options = *bindings; owner->options.initial_configuration = configuration;
    owner->initial = initial; owner->state = state;
    frontend_remote_prediction_source source = {0}; bool present = false;
    if (state.initialized) ok = bindings->source_read(bindings->context, &source, &present, error) &&
        present && source_valid(owner, &source) && same_identity(&state, &source) &&
        bindings->source_current(bindings->context, &source);
    if (!ok) {
        frontend_remote_prediction_destroy(owner);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Restored prediction lost its genuine source graph");
        return false;
    }
    *out = owner;
    return true;
}
