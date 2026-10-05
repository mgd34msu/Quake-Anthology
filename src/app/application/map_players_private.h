#ifndef QA_APPLICATION_MAP_PLAYERS_PRIVATE_H
#define QA_APPLICATION_MAP_PLAYERS_PRIVATE_H

#include "map_private.h"
#include "qa/application_players.h"
#include "qa/modes_map.h"
#include "qa/game_q1_travel.h"

typedef struct application_guest_carry {
    qa_actor_owner owner;
    qa_sha256_digest identity;
} application_guest_carry;
typedef struct application_player_carry {
    qa_combat_state combat;
    float maximum_health;
    qa_inventory_entry *inventory;
    size_t count;
    qa_q1_player_view q1;
    qa_q1_mg3_progress mg3;
    qa_q1_travel_state *q1_source;
    qa_q2_player_carry q2;
    qa_q2_weapon weapon2;
    qa_q3_weapon weapon3;
    qa_q3_fire_stamp fire3;
    int32_t arsenal3_time_ms;
    qa_q3_usercmd q3_command;
    qa_actor_id q3_previous_actor;
    qa_actor_owner character_owner, arsenal_owner;
    application_guest_carry guests[7];
    size_t guest_count;
    bool addon_reset, arsenal_addon_reset;
    bool present, has_q1, has_mg3, has_q2, has_weapon2, has_weapon3;
    bool q3_client;
} application_player_carry;
typedef struct application_player_guest_binding {
    qa_actor_owner owner;
    qa_sha256_digest identity;
    uint32_t source_slot;
} application_player_guest_binding;
typedef struct application_player_record {
    uint32_t seat, client_slot, source_slot;
    qa_actor_id actor, configured_actor;
    application_provider *character;
    uint64_t deferred_until_ns;
    qa_net_client_id remote_client;
    qa_net_seat_id remote_seat;
    char *name, *team, *skin, *userinfo;
    char *bot_definition;
    float bot_skill;
    int32_t bot_delay_ms;
    application_player_guest_binding *guests;
    size_t guest_count;
    qa_q1_travel_state *q1_entry;
    bool deferred, spectator, bot, remote, dynamic, retiring, source_begin_pending;
} application_player_record;
typedef struct application_player_point {
    qa_mode_spawnpoint point;
    qa_string_id target;
    uint32_t ordinal;
} application_player_point;
struct application_player_roster {
    uint64_t revision;
    application_player_record *records;
    size_t count, capacity;
    application_player_point *points;
    size_t point_count;
    qa_q1_spawn_point *q1_points;
    size_t q1_point_count;
    qa_q1_spawn_selector *q1_selector;
    application_provider *map_provider;
    qa_bsp_family family;
    qa_string_id spawn_point;
    int32_t world_type;
};
struct application_player_travel {
    struct application_player_roster *roster;
    application_player_carry *carry;
    qa_launch_seat *seats;
    size_t count, point_capacity;
    qa_q2_landmark landmark;
    bool carry_players, new_unit, has_landmark;
};

typedef struct application_q3_round_player_admission {
    const qa_q3_usercmd *command;
} application_q3_round_player_admission;

bool application_q3_player_spawn_pose(qa_application *, qa_actor_id, bool spectator,
                                      qa_body_state *, bool *found, qa_error *);
bool application_q3_find_intermission_pose(application_provider *, qa_vec3 *origin,
    qa_vec3 *angles, qa_error *);

bool application_players_guest_attach(qa_application *, application_provider *, uint32_t,
                                       qa_actor_id, const qa_builtin_player_info *, qa_error *);
bool application_players_guest_detach(qa_application *, application_provider *, uint32_t,
                                       qa_actor_id, qa_error *);

bool application_players_checkpoint_capture(qa_application *, qa_buffer *, qa_error *);
bool application_players_checkpoint_restore(qa_application *candidate, qa_bytes, qa_error *);
bool application_players_campaign_prepare(qa_application *, bool carry_players, const qa_q2_landmark *,
    application_player_travel **, qa_error *);
bool application_players_campaign_consume(qa_application *, application_publication *,
    application_player_travel **, qa_error *);
bool application_players_campaign_exclude(qa_application *, qa_error *);
bool application_players_campaign_reenter(qa_application *candidate, qa_application *previous,
    application_player_travel *, const char *spawn_point, qa_error *);
bool application_player_qw_spectator(const application_provider *, const application_player_record *);
bool application_players_native_q3_retire(qa_application *, application_provider *,
    qa_actor_id, qa_error *);
bool application_players_bot_detach(qa_application *,qa_actor_id,qa_error *);
bool application_players_source_disconnect(qa_application *,application_provider *,qa_actor_id,qa_error *);
bool application_players_character_disconnect(qa_application *,application_provider *,qa_actor_id,qa_error *);
bool application_players_component_retire(qa_application *,application_provider *,qa_actor_id,qa_error *);
bool application_players_bot_allocate(qa_application *,const qa_launch_seat *,int32_t *,qa_error *);
bool application_players_bot_begin(qa_application *,uint32_t,qa_error *);
bool application_player_equipment_selection(qa_application *, const qa_launch_choices *,
    qa_actor_id, uint32_t seat, qa_equipment_selection *, qa_equipment_source_selection *, qa_error *);
bool application_players_native_q1_respawn(qa_application *, application_provider *,
    qa_actor_id, qa_q1_travel_state *, bool force, qa_error *);
bool application_players_native_q1_spawn_pose(qa_application *, application_provider *,
    qa_actor_id, qa_body_state *, bool *found, qa_error *);
bool application_players_selected_character_respawn(void *, qa_actor_id, qa_error *);
bool application_players_source_spawned(void *, qa_actor_id, qa_error *);
bool application_players_declared_clients_admit(qa_application *, qa_error *);

#endif
