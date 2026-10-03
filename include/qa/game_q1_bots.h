#ifndef QA_GAME_Q1_BOTS_H
#define QA_GAME_Q1_BOTS_H
#include "qa/game_q1.h"

typedef struct qa_q1_bot_entity {
    qa_string_id model,classname;
    float max_health;
    int32_t frame;
    bool present,worldspawn;
} qa_q1_bot_entity;
bool qa_q1_bot_entity_read(const qa_q1_game *,qa_actor_id,qa_q1_bot_entity *,qa_error *);
bool qa_q1_bot_clock_read(const qa_q1_game *,double *,bool *,double *,qa_error *);
bool qa_q1_bot_max_clients(const qa_q1_game *,uint32_t *,qa_error *);
qa_actor_id qa_q1_bot_world_actor(const qa_q1_game *);
bool qa_q1_bot_weapon_read(qa_q1_game *,qa_actor_id,qa_q1_weapon,
                           qa_q1_weapon_view *,bool *covered,qa_error *);
bool qa_q1_bot_weapon_usable(qa_q1_game *,qa_actor_id,qa_q1_weapon,bool *,qa_error *);
bool qa_q1_bot_weapon_items(const qa_q1_game *,qa_q1_weapon,qa_item_id *,qa_item_id *,
                            bool *covered,qa_error *);
bool qa_q1_bot_weapon_state_read(const qa_q1_game *,qa_actor_id,qa_q1_weapon *,
                                 double *attack_finished,double *time, bool *present,qa_error *);
typedef struct qa_q1_source_client_view {
    qa_actor_id actor;
    uint32_t slot;
    const char *name;
    float frags,team;
    uint8_t shirt,pants;
    bool observer,no_target,god_mode;
    qa_actor_id spectator_goal,spectator_track;
    /* Physical edict cursor and 1-based engine spec_track survive actor reuse. */
    uint32_t spectator_goal_ordinal,spectator_track_slot;
    int32_t impulse;
    bool use,death_recorded;
    double respawn_requested_at;
} qa_q1_source_client_view;
typedef struct qa_q1_source_client_services {
    void *context;
    bool (*publish)(void *,const qa_q1_source_client_view *,qa_error *);
    bool (*observer)(void *,qa_actor_id,bool,qa_error *);
} qa_q1_source_client_services;
bool qa_q1_source_clients_configure(qa_q1_game *,const qa_q1_source_client_services *,qa_error *);
bool qa_q1_source_client_read(const qa_q1_game *,qa_actor_id,qa_q1_source_client_view *);
bool qa_q1_source_client_toggle_notarget(qa_q1_game *,qa_actor_id,bool *,qa_error *);
bool qa_q1_source_client_userinfo(qa_q1_game *,qa_actor_id,const char *,qa_error *);
bool qa_q1_source_client_userinfo_storage(qa_q1_game *,qa_actor_id,const char *,qa_error *);
/* The separately admitted name is used only when received userinfo has no name
 * key. It is a typed source value, so original Q1 backslashes remain intact. */
bool qa_q1_source_client_userinfo_named(qa_q1_game *,qa_actor_id,const char *,const char *,qa_error *);
/* Continuations can retain name separately from delimiter-based userinfo. */
bool qa_q1_source_client_userinfo_read(const qa_q1_game *,qa_actor_id,bool include_name,qa_buffer *,qa_error *);
/* Publishes the exact name admitted by the source command boundary. */
bool qa_q1_source_client_name(qa_q1_game *,qa_actor_id,const char *,qa_error *);
bool qa_q1_source_client_colors(qa_q1_game *,qa_actor_id,int32_t,int32_t,qa_error *);
bool qa_q1_source_client_info(const qa_q1_game *,qa_actor_id,const char *,const char **);
bool qa_q1_source_client_add_score(qa_q1_game *,qa_actor_id,double delta,qa_error *);
bool qa_q1_source_client_set_score(qa_q1_game *,qa_actor_id,float score,qa_error *);
bool qa_q1_source_client_observer(qa_q1_game *,qa_actor_id,bool,qa_error *);
bool qa_q1_source_client_disconnect_sound(qa_q1_game *,qa_actor_id,qa_error *);
bool qa_q1_character_disconnect_pose(qa_q1_game *,qa_actor_id,bool *applied,qa_error *);
/* NONE is the genuine Source world find cursor, rather than a player alias. */
bool qa_q1_source_spectator_goal_reset(qa_q1_game *,qa_actor_id,qa_error *);
bool qa_q1_source_spectator_goal_next(qa_q1_game *,qa_actor_id,qa_actor_id *,bool *,qa_error *);
bool qa_q1_source_spectator_track(qa_q1_game *,qa_actor_id,qa_actor_id,qa_error *);
bool qa_q1_source_client_spawned(qa_q1_game *,qa_actor_id,qa_error *);
/* Mark the genuine source client's death before obituary, score and drop
 * callbacks. Already recorded deaths return first=false without replay. */
bool qa_q1_source_client_record_death(qa_q1_game *,qa_actor_id,bool *first,qa_error *);
bool qa_q1_source_client_consume_impulse(qa_q1_game *,qa_actor_id,qa_error *);
bool qa_q1_source_respawn_options_read(const qa_q1_game *,qa_q1_options *,double *source_seconds,qa_error *);
/* Pure imported constructor policy, available only before source restore
 * finish. It admits no gameplay, source clock or callbacks. */
bool qa_q1_source_respawn_options_prepared(const qa_q1_game *,qa_q1_options *,qa_error *);
bool qa_q1_source_client_request_respawn(qa_q1_game *,qa_actor_id,bool *force_spawn,qa_error *);
bool qa_q1_bot_exit_level(qa_q1_game *,double seconds,bool same_level,qa_error *);
/* The caller frees genuine supply receipts with qa_supply_preview_free.
 * found qualifies a source weapon/ammo offer; eligible also requires its
 * current source touch lifecycle and recipient admission. */
bool qa_q1_bot_supply_preview(qa_q1_game *,qa_actor_id pickup,qa_actor_id recipient,
                              qa_supply_preview_result *,bool *eligible,bool *found,qa_error *);
#endif
