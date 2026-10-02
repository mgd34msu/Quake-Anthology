#ifndef QA_BOTS_H
#define QA_BOTS_H

#include "qa/bot_knowledge.h"
#include "qa/bot_perception.h"
#include "qa/modes.h"
#include "qa/console.h"
#include "qa/network_q3.h"
#include "qa/bots_player.h"

typedef struct qa_bots qa_bots;
typedef enum qa_bot_decision {
    QA_BOT_INTERMISSION, QA_BOT_OBSERVER, QA_BOT_RESPAWNING, QA_BOT_STANDING,
    QA_BOT_ACTIVATING, QA_BOT_SEEK_NEARBY, QA_BOT_SEEK_LONG_TERM,
    QA_BOT_FIGHTING, QA_BOT_CHASING, QA_BOT_RETREATING, QA_BOT_BATTLE_NEARBY
} qa_bot_decision;
typedef enum qa_bot_order_kind { QA_BOT_ORDER_NONE, QA_BOT_ORDER_POINT, QA_BOT_ORDER_FOLLOW } qa_bot_order_kind;
typedef enum qa_bot_order_status { QA_BOT_ORDER_ERROR, QA_BOT_ORDER_SUCCESS, QA_BOT_ORDER_ACTIVE } qa_bot_order_status;
typedef struct qa_bot_order {
    qa_bot_order_kind kind;
    qa_bot_order_status status;
    qa_vec3 point;
    qa_actor_id target;
} qa_bot_order;
typedef enum qa_bot_source_callback {
    QA_BOT_PRE_THINK, QA_BOT_POST_THINK, QA_BOT_BEGIN_FRAME, QA_BOT_END_FRAME
} qa_bot_source_callback;
/* Values are detached observations of the selected character and arsenal.
 * They carry no storage authority over the canonical actor or inventory. */
typedef struct qa_bot_player {
    bool connected, observer, intermission, dead, grounded, crouched, teleported;
    bool water_jump, grapple_pull, firing, invisible, chatting;
    bool carrying_objective;
    qa_vec3 origin, velocity, eye, view_angles;
    int32_t delta_angles[3];
    uint32_t presence;
    int32_t current_weapon, weapon_state, weapon_time_ms;
    qa_actor_id last_attacker, last_victim;
    int32_t deaths, kills, last_damage_cause;
    float air_time, teleport_time;
    uint64_t spawn_sequence;
    uint64_t teleport_sequence;
} qa_bot_player;
typedef struct qa_bot_entity {
    qa_bot_entity_update observation;
    int32_t number;
    bool present, linked, hidden, missile, grapple, temporary_event, proximity_trigger;
} qa_bot_entity;
typedef struct qa_bot_controls {
    int32_t think_time_ms;
    bool paused, challenge, fast_chat, no_chat, rocket_jump, grapple, report;
} qa_bot_controls;
typedef struct qa_bot_activation {
    qa_actor_id blocker, target;
    qa_bot_goal goal;
    qa_vec3 blocker_origin, target_origin, aim;
    bool shoot;
} qa_bot_activation;
struct qa_bot_source_activation;
/* Readers borrow the actual BotState words at the reached Source stage. */
typedef struct qa_bot_activation_query {
    void *context;
    bool (*origin)(void *,qa_vec3 *,qa_error *);
    bool (*eye)(void *,qa_vec3 *,qa_error *);
    bool (*area)(void *,int32_t *,qa_error *);
    bool (*travel_flags)(void *,uint32_t *,qa_error *);
    bool (*top)(void *,struct qa_bot_source_activation *,bool *,qa_error *);
    bool (*developer)(void *,bool *,qa_error *);
    float time;
} qa_bot_activation_query;
typedef struct qa_bot_admission {
    qa_actor_id actor;
    uint32_t client;
    int32_t entity;
    const char *character_file, *name, *team;
    float skill;
    /* Objectives/orders select this explicit independent mode. Canonical
     * connection, body and inventory remain the ordinary shared owners. */
    qa_mode_id mode;
    bool team_arena;
    bool restart;
} qa_bot_admission;
typedef struct qa_bot_view {
    qa_actor_id actor, enemy;
    uint32_t client;
    int32_t source_client;
    int32_t entity, weapon;
    qa_mode_id mode;
    qa_bot_decision decision;
    qa_bot_order order;
    float enter_time, think_time;
} qa_bot_view;
typedef struct qa_bot_source_player {
    bool present, bot;
    qa_vec3 origin;
} qa_bot_source_player;
typedef struct qa_bot_source_player_state {
    bool present, has_player;
    int32_t pm_type, score, last_hurt_client, last_hurt_mod;
} qa_bot_source_player_state;
typedef struct qa_bot_source_row {
    qa_q3_entity state;
    qa_string_id classname;
    bool present;
} qa_bot_source_row;
typedef struct qa_bot_source_memory {
    void *context;
    bool (*allocate)(void *,uint32_t,uint32_t *,qa_error *);
    bool (*read)(void *,uint32_t,void *,uint32_t,qa_error *);
    bool (*write)(void *,uint32_t,const void *,uint32_t,qa_error *);
    bool (*borrow_span)(void *,uint32_t,uint32_t,uint8_t **,qa_error *);
} qa_bot_source_memory;
typedef struct qa_bot_services {
    void *context;
    qa_bot_source_memory memory;
    bool team_arena;
    qa_builtin_services shared;
    qa_modes *modes;
    /* Optional source output is detached only until the BotAI copy stage. */
    bool (*player)(void *, qa_actor_id, qa_bot_player *, qa_q3_player *, qa_error *);
    bool (*inventory)(void *, qa_actor_id, const qa_bot_player *,
                       const qa_bot_player_state_view *,const qa_bot_inventory_target *, qa_error *);
    bool (*entity)(void *, qa_actor_id, qa_bot_entity *, qa_error *);
    qa_actor_id (*entity_actor)(void *, int32_t entity_number);
    bool (*entity_extent)(void *, uint32_t *, qa_error *);
    bool (*entity_list)(void *, qa_builtin_actor_snapshot *, qa_error *);
    bool (*arsenal)(void *, qa_actor_id, const qa_bot_weapon_knowledge **, size_t *,
                     void **lease, qa_error *);
    void (*arsenal_end)(void *, void *lease);
    /* Submit to normal actor command admission, which interprets actions using
     * the selected movement/arsenal. Source Q3 command is supplied as well for
     * its exact byte/angle semantics; foreign providers use semantic input. */
    bool (*submit)(void *, qa_actor_id, const qa_bot_input *, const qa_movement_command *, qa_error *);
    bool (*console)(void *, qa_actor_id, char *text, size_t, bool *found, qa_error *);
    /* Source service identities differ from the shared library namespace. */
    bool (*source_client)(void *, qa_actor_id, int32_t *, qa_error *);
    qa_actor_id (*source_actor)(void *, int32_t);
    qa_cvars *(*configuration)(void *);
    bool (*register_cvar)(void *, const char *, const char *, uint32_t, qa_error *);
    bool (*configstring)(void *, uint32_t, char *, size_t, qa_error *);
    bool (*set_configstring)(void *, uint32_t, const char *, qa_error *);
    bool (*source_generic1)(void *, int32_t, int32_t *, qa_error *);
    bool (*source_player)(void *, int32_t, qa_bot_source_player *, qa_error *);
    bool (*source_player_state)(void *, int32_t, qa_bot_source_player_state *, qa_error *);
    bool (*source_intermission)(void *, bool *, qa_error *);
    bool (*source_row_count)(void *, uint32_t *, qa_error *);
    bool (*source_row)(void *, int32_t, qa_bot_source_row *, qa_error *);
    bool (*snapshot_entity)(void *, qa_actor_id, int32_t, int32_t *, bool *, qa_error *);
    bool (*source_entity)(void *, int32_t, qa_q3_entity *, bool *, qa_error *);
    bool (*source_event_time)(void *, int32_t, int32_t *, qa_error *);
    bool (*print)(void *, const char *, qa_error *);
    bool (*userinfo)(void *, qa_actor_id, const char *, const char *, qa_error *);
    bool (*get_userinfo)(void *, qa_actor_id, char *, size_t, qa_error *);
    bool (*set_userinfo)(void *, qa_actor_id, const char *, qa_error *);
    bool (*source_game_type)(void *, int32_t *, qa_error *);
    bool (*exit_level)(void *, qa_error *);
    bool (*insert_console_command)(void *, const char *, qa_error *);
    /* GAME owns this draw and its checkpoint. Botlib has a separate RNG. */
    bool (*random)(void *, float *, qa_error *);
    bool (*check_spawn)(void *, qa_error *);
    bool (*command)(void *, qa_actor_id, const char *, qa_error *);
    bool (*activation)(void *, qa_actor_id bot, int32_t blocker_entity,
                        qa_bot_activation *, bool *found, qa_error *);
    bool (*source_activation)(void *,qa_actor_id bot,int32_t blocker_entity,
        const qa_bot_activation_query *,struct qa_bot_source_activation *,int32_t *bsp_entity,qa_error *);
    bool (*source_model_bounds)(void *,int32_t model,int32_t entity_type,int32_t contents,
        qa_vec3 *mins,qa_vec3 *maxs,int32_t *entity,qa_error *);
    bool (*predict_motion)(void *, qa_actor_id target,
                            const qa_bot_movement_prediction_query *,
                            qa_bot_movement_prediction *, bool *available, qa_error *);
    void (*diagnostic)(void *, qa_script_severity, const char *);
} qa_bot_services;
/* The runtime is borrowed and shared with botlib hosts. Actor allocation and
 * connection membership are supplied by the existing session/mode owners. */
/* Initial source setup registers actual cached cvars before library setup and
 * loads the real registered map before deathmatch AI. Nonzero result preserves
 * the library's actual setup error code and leaves no published population. */
bool qa_bots_create_source(qa_bot_runtime *,const qa_bot_services *,const char *,int32_t *,qa_bots **,qa_error *);
/* Fresh round AI over the retained initialized library. No map reload or
 * actor admission occurs here; the caller reconnects actual preserved clients. */
bool qa_bots_create_round(qa_bot_runtime *, const qa_bot_services *, qa_bots **, qa_error *);
/* Borrow the actual admitted settings until the population is mutated. */
bool qa_bots_admission_read(const qa_bots *, qa_actor_id, qa_bot_admission *, qa_error *);
/* Reads the current source ws word from this actor's genuine retained record.
 * The numeric word remains source state; this call does not create a handle. */
bool qa_bots_source_weapon_handle(qa_bots *,qa_actor_id,uint32_t *,qa_error *);
/* Construct an empty isolated population with the actual saved client capacity.
 * The runtime address remains stable; source setup/admission/frame callbacks do
 * not run. Its private continuation must restore before ordinary bot use. */
bool qa_bots_create_restored(qa_bot_runtime *, const qa_bot_services *, uint32_t client_capacity,
                            qa_bots **, qa_error *);
/* Retains the population when one of its synchronous callbacks is active. */
bool qa_bots_destroy(qa_bots *, qa_error *);
/* Source shutdown runs while the actual clients can still receive commands.
 * Pure destruction also serves failed admission/restore owners and has no
 * source session/chat effects. */
bool qa_bots_shutdown(qa_bots *, bool restart, qa_error *);
bool qa_bots_shutdown_client(qa_bots *, qa_actor_id, bool restart, qa_error *);
bool qa_bots_can_destroy(const qa_bots *);
bool qa_bots_admit(qa_bots *, const qa_bot_admission *, qa_error *);
/* False accepted is the actual source SetupClient return, including a retained
 * allocation whose raw inuse word is already set. Service errors return false. */
bool qa_bots_admit_source(qa_bots *,const qa_bot_admission *,bool *accepted,qa_error *);
bool qa_bots_setup_failed(const qa_bots *,qa_actor_id);
bool qa_bots_release(qa_bots *, qa_actor_id, qa_error *);
bool qa_bots_actor_released(qa_bots *, const qa_actor_record *, qa_error *);
bool qa_bots_frame(qa_bots *, int32_t source_time_ms, qa_error *);
bool qa_bots_interbreed_end_admitted(const qa_bots *);
bool qa_bots_interbreed_end_match(qa_bots *, qa_error *);
bool qa_bots_test_aas(qa_bots *, qa_vec3, qa_error *);
bool qa_bots_level_reset(qa_bots *, qa_error *);
bool qa_bots_source_begin(qa_bots *,qa_actor_id,qa_vec3,int32_t weapon,qa_error *);
bool qa_bots_source_memory_bind(qa_bots *,qa_error *);
bool qa_bots_read(const qa_bots *, qa_actor_id, qa_bot_view *, qa_error *);
bool qa_bots_move_to(qa_bots *, qa_actor_id, qa_vec3, qa_bot_order_status *, qa_error *);
bool qa_bots_follow(qa_bots *, qa_actor_id, qa_actor_id target, qa_bot_order_status *, qa_error *);
bool qa_bots_clear_order(qa_bots *, qa_actor_id, qa_error *);
qa_bot_order_status qa_bots_order_status(const qa_bots *, qa_actor_id);
typedef struct qa_bots_checkpoint qa_bots_checkpoint;
/* Capture owns private decisions, actions, goals, movement, chat queues and
 * learned weights. The canonical actors and shared session RNG are separate
 * checkpoint owners. Restore requires those same actor/resource bindings. */
bool qa_bots_capture(qa_bots *, qa_bots_checkpoint **, qa_error *);
bool qa_bots_checkpoint_validate(qa_bots *, const qa_bots_checkpoint *, qa_error *);
bool qa_bots_restore(qa_bots *, const qa_bots_checkpoint *, qa_error *);
void qa_bots_checkpoint_destroy(qa_bots_checkpoint *);

#endif
