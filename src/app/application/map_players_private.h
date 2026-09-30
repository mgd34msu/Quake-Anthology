#ifndef QA_APPLICATION_MAP_PLAYERS_PRIVATE_H
#define QA_APPLICATION_MAP_PLAYERS_PRIVATE_H

#include "map_private.h"
#include "qa/application_players.h"
#include "qa/modes_map.h"

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
    qa_q2_player_carry q2;
    qa_q2_weapon weapon2;
    qa_q3_weapon weapon3;
    qa_actor_owner character_owner, arsenal_owner;
    application_guest_carry guests[7];
    size_t guest_count;
    bool addon_reset, arsenal_addon_reset;
    bool present, has_q1, has_mg3, has_q2, has_weapon2, has_weapon3;
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
    application_player_guest_binding *guests;
    size_t guest_count;
    bool deferred, spectator, bot, remote, dynamic, retiring, source_begin_pending;
} application_player_record;
typedef struct application_player_point {
    qa_mode_spawnpoint point;
    qa_string_id target;
    uint32_t ordinal;
} application_player_point;
struct application_player_roster {
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

bool application_players_guest_attach(qa_application *, application_provider *, uint32_t,
                                       qa_actor_id, const qa_builtin_player_info *, qa_error *);
bool application_players_guest_detach(qa_application *, application_provider *, uint32_t,
                                       qa_actor_id, qa_error *);

bool application_players_checkpoint_capture(qa_application *, qa_buffer *, qa_error *);
bool application_players_checkpoint_restore(qa_application *candidate, qa_bytes, qa_error *);

#endif
