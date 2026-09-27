#ifndef QA_GAME_Q2_PLAYER_H
#define QA_GAME_Q2_PLAYER_H
#include "qa/game_q2.h"
#include "qa/game_q2_entities.h"

typedef struct qa_q2_player_movement {
    qa_vec3 view_angles, command_angles;
    qa_bounds standing_bounds;
    uint32_t buttons, water_type;
    int water_level;
    float impact_delta;
    bool grounded, grounded_on_world, ducked, animate_q2, on_ladder, grapple_attached, noclip;
    uint64_t grapple_released_until_ns;
} qa_q2_player_movement;
typedef enum qa_q2_player_motion_kind {
    QA_Q2_PLAYER_SPAWN,
    QA_Q2_PLAYER_TELEPORT,
    QA_Q2_PLAYER_FREEZE,
    QA_Q2_PLAYER_NOCLIP
} qa_q2_player_motion_kind;
typedef struct qa_q2_player_motion {
    qa_q2_player_motion_kind kind;
    qa_vec3 origin, velocity, angles, command_angles;
    uint64_t hold_ns;
    bool spectator, enabled;
} qa_q2_player_motion;
typedef struct qa_q2_blend {
    float x, y, z, w;
} qa_q2_blend;
typedef struct qa_q2_player_view {
    qa_vec3 angles, offset, kick_angles, gun_angles, gun_offset;
    qa_q2_blend blend;
    float fov, health, armor, ammo;
    int score, flashes, layouts;
    qa_item_id selected_item, timer_item;
    int timer_seconds;
    bool underwater, spectator;
} qa_q2_player_view;
typedef struct qa_q2_score_row {
    uint32_t slot;
    const char *name;
    int score, ping, minutes;
    bool spectator;
} qa_q2_score_row;
typedef enum qa_q2_respawn_status {
    QA_Q2_RESPAWN_READY,
    QA_Q2_RESPAWN_COMBAT,
    QA_Q2_RESPAWN_BAD_AREA,
    QA_Q2_RESPAWN_BLOCKED,
    QA_Q2_RESPAWN_WAITING,
    QA_Q2_RESPAWN_NO_LIVES
} qa_q2_respawn_status;
typedef enum qa_q2_player_event_kind {
    QA_Q2_PLAYER_PRINT,
    QA_Q2_PLAYER_USERINFO,
    QA_Q2_PLAYER_STUFFTEXT,
    QA_Q2_PLAYER_VIEW,
    QA_Q2_PLAYER_SCOREBOARD,
    QA_Q2_PLAYER_INVENTORY,
    QA_Q2_PLAYER_HELP,
    QA_Q2_PLAYER_LOAD_MENU,
    QA_Q2_PLAYER_TRAIL,
    QA_Q2_PLAYER_CHASE,
    QA_Q2_PLAYER_FLASHLIGHT,
    QA_Q2_PLAYER_DOGTAG,
    QA_Q2_PLAYER_RESPAWN_STATUS,
    QA_Q2_PLAYER_RESTART,
    QA_Q2_PLAYER_DIRECTIONAL_DAMAGE,
    QA_Q2_PLAYER_HELP_PATH,
    QA_Q2_PLAYER_ALPHA
} qa_q2_player_event_kind;
typedef struct qa_q2_player_event {
    qa_q2_player_event_kind kind;
    qa_actor_id actor, target;
    const char *text, *skin;
    qa_q2_player_view view;
    const qa_q2_score_row *scores;
    const qa_inventory_entry *inventory;
    size_t count;
    qa_vec3 origin, direction;
    uint64_t time_ns;
    qa_item_id selected_item;
    uint32_t slot;
    int level, lives;
    float damage, alpha;
    qa_q2_respawn_status respawn_status;
    qa_q2_hand hand;
    bool visible, reliable, health, armor, shield, first;
} qa_q2_player_event;
typedef struct qa_q2_player_rules {
    const char *password, *spectator_password, *spawn_point, *map_name, *start_items;
    uint32_t max_spectators, max_clients;
    bool cheats, coop_squad_respawn, coop_instanced_items, coop_lives, coop_player_collision;
    int coop_num_lives;
    bool force_respawn, no_fall_damage, spawn_farthest;
    float force_respawn_seconds;
    bool deathmatch_allow_exit;
    float autosave_minimum_seconds;
    unsigned flood_messages;
    float flood_seconds, flood_wait_seconds;
    float roll_speed, roll_angle, run_pitch, run_roll, bob_up, bob_pitch, bob_roll;
    qa_vec3 gun_offset;
} qa_q2_player_rules;
typedef struct qa_q2_character_weapon {
    qa_q2_weapon q2_weapon;
    qa_item_id ammo;
    qa_vec3 kick_angles, kick_origin;
    qa_string_id loop_sound;
} qa_q2_character_weapon;
typedef struct qa_q2_player_services {
    void *context;
    /* Read-only projection of shared cinematic control. The application owns
     * its pose, movement continuation and lifetime, including reset at spawn. */
    bool (*controlled)(void *, qa_actor_id);
    bool (*movement)(void *, qa_actor_id, qa_q2_player_movement *, qa_error *);
    bool (*set_movement)(void *, qa_actor_id, const qa_q2_player_motion *, qa_error *);
    bool (*emit)(void *, const qa_q2_player_event *, qa_error *);
    bool (*weapon_state)(void *, qa_actor_id, qa_q2_character_weapon *, qa_error *);
    bool (*weapon_input)(void *, qa_actor_id, qa_q2_weapon_input *, qa_error *);
    bool (*banned)(void *, const char *address);
    bool (*score)(void *, qa_actor_id victim, qa_actor_id attacker, qa_actor_id recipient,
                  int change, int means, qa_error *);
    /* Nonmutating read. Both score callbacks address the same selected scoring
     * context and must be provided together. */
    bool (*score_read)(void *, qa_actor_id, int32_t *, qa_error *);
    bool (*spawned)(void *, qa_actor_id, qa_error *);
    bool (*persistent_inventory)(void *, qa_actor_id, qa_error *);
    bool (*select_spawn)(void *, qa_actor_id, qa_vec3 *, qa_vec3 *, bool *, qa_error *);
    bool (*death)(void *, qa_actor_id, const qa_attack *, qa_error *);
    bool (*drop_inventory)(void *, qa_actor_id, const qa_attack *, qa_error *);
    bool (*before_death_inventory)(void *, qa_actor_id, const qa_attack *, qa_error *);
    bool (*disconnect)(void *, qa_actor_id, qa_error *);
    bool (*command)(void *, qa_actor_id, const char *, size_t, const char *const *, bool *,
                    qa_error *);
    bool (*grant_arsenal)(void *, qa_actor_id, bool ammo, bool *, qa_error *);
    bool (*give_item)(void *, qa_actor_id, size_t, const char *const *, bool *, qa_error *);
    bool (*player_collision)(void *, qa_actor_id, bool, qa_error *);
} qa_q2_player_services;
typedef struct qa_q2_player_carry {
    float health, maximum_health;
    qa_armor armor;
    qa_inventory_entry *inventory;
    size_t count;
    qa_q2_weapon weapon;
    qa_item_id selected_item;
    int score;
    uint32_t flags, power_cubes;
} qa_q2_player_carry;
typedef struct qa_q2_player_admission {
    uint32_t slot, seat;
    const char *userinfo, *social_id;
    bool initialize_inventory, use_q2_weapons, use_q2_inventory, bot;
    const qa_q2_player_carry *carry;
} qa_q2_player_admission;
typedef struct qa_q2_connection_result {
    bool allowed;
    char userinfo[2304], reason[128];
} qa_q2_connection_result;
typedef struct qa_q2_player_info {
    uint32_t slot, seat;
    char name[32], skin[256];
    int score, ping, lives;
    qa_actor_id chase_target;
    qa_item_id selected_item;
    float view_height;
    bool connected, spectator, dead, god, notarget, noclip, flashlight;
} qa_q2_player_info;
typedef struct qa_q2_player_noise_record {
    qa_actor_id owner;
    qa_vec3 origin;
    uint64_t time_ns;
    bool present;
} qa_q2_player_noise_record;
typedef struct qa_q2_player_state {
    qa_q2_player_info info;
    qa_q2_visual visual;
    qa_q2_player_carry coop;
    qa_inventory_entry *spawn_inventory;
    size_t spawn_count;
    char userinfo[2048], social_id[128], dogtag[256];
    qa_q2_hand hand;
    int gender, old_water, drown_damage, breather_sound, animation_priority, animation_end;
    int auto_switch, auto_shield, flashes;
    uint32_t buttons, latched_buttons, event;
    uint64_t entered_ns, respawn_ns, air_ns, drown_ns, pain_ns, damage_ns, power_armor_ns;
    uint64_t fall_ns, landmark_noise_ns, flood_until_ns, flood_times[10];
    uint64_t slime_ns, animation_ns, last_damage_ns, last_firing_ns, invisibility_fade_ns;
    uint64_t tracker_ns, nuke_ns, flash_ns, respawn_timeout_ns, grapple_released_ns, quake_ns;
    uint64_t help_draw_ns, help_marker_ns, mission_time_ns;
    uint32_t mission_primary, mission_secondary;
    unsigned mission_changed;
    float fov, damage_blood, damage_armor, damage_power, damage_knockback;
    float damage_alpha, bonus_alpha, damage_pitch, damage_roll, fall_value;
    float bob_time, bob_move, killer_yaw;
    qa_vec3 damage_from, damage_blend, old_velocity, old_view_angles, slow_view_angles;
    qa_vec3 help_location, *help_points;
    size_t help_count, help_index, help_capacity;
    qa_string_id loop_sound, help_image;
    qa_actor_id noise[2], sphere_camera;
    qa_q2_fog fog, wanted_fog;
    float fog_transition;
    size_t flood_count;
    bool use_weapons, use_inventory, requested_spectator, bot, gibbed, weapon_thunk;
    bool animation_duck, animation_run, landmark_free_fall;
    bool show_scores, show_inventory, show_help, bob_skip, nuke_inside, auto_shield_enabled;
    bool awaiting_respawn, spawned, player_collision, has_coop, has_pending_landmark, squad_spawn,
        corpse;
    qa_q2_landmark pending_landmark;
    qa_vec3 squad_origin, squad_angles;
} qa_q2_player_state;
typedef struct qa_q2_player_checkpoint {
    uint32_t version;
    bool present;
    qa_q2_player_state value;
    qa_q2_saved_reference chase_target, noise[2], sphere_camera, landmark_player;
} qa_q2_player_checkpoint;
typedef struct qa_q2_players_checkpoint {
    uint32_t version;
    qa_q2_saved_reference corpses[8], landmark_player, noise_owner[2];
    unsigned corpse_index, death_animation, pain_animation;
    bool intermission, exit, camera_set, has_landmark, deadly_killbox;
    uint32_t intermission_flags;
    uint64_t intermission_ns, fade_ns, restart_ns;
    qa_string_id next_map;
    qa_q2_landmark landmark;
    qa_vec3 camera_origin, camera_angles;
    qa_q2_player_noise_record noise[2];
} qa_q2_players_checkpoint;
/* Capture owns the variable arrays. Save codecs encode fields and remap item
 * and resource identities; embedded actor IDs are zeroed in favor of saved
 * references. Restore follows shared stores and emits no gameplay callbacks. */
bool qa_q2_player_capture(qa_q2_game *, qa_actor_id, qa_q2_player_checkpoint *, qa_error *);
bool qa_q2_player_restore(qa_q2_game *, qa_actor_id, const qa_q2_player_checkpoint *, qa_error *);
void qa_q2_player_checkpoint_free(qa_q2_player_checkpoint *);
bool qa_q2_players_capture(qa_q2_game *, qa_q2_players_checkpoint *, qa_error *);
bool qa_q2_players_restore(qa_q2_game *, const qa_q2_players_checkpoint *, qa_error *);
bool qa_q2_player_noise_read(const qa_q2_game *, bool secondary, qa_q2_player_noise_record *);
bool qa_q2_player_help_computer(qa_q2_game *, qa_actor_id, qa_error *);
void qa_q2_player_rules_default(qa_q2_player_rules *);
bool qa_q2_players_configure(qa_q2_game *, const qa_q2_player_rules *,
                             const qa_q2_player_services *, qa_error *);
bool qa_q2_player_connect(qa_q2_game *, const char *, bool bot, qa_q2_connection_result *,
                          qa_error *);
bool qa_q2_player_admit(qa_q2_game *, qa_actor_id, const qa_q2_player_admission *, qa_error *);
bool qa_q2_player_userinfo(qa_q2_game *, qa_actor_id, const char *, qa_error *);
bool qa_q2_player_read(qa_q2_game *, qa_actor_id, qa_q2_player_info *);
bool qa_q2_player_controlled(const qa_q2_game *, qa_actor_id);
bool qa_q2_player_projection(qa_q2_game *, qa_actor_id, qa_builtin_player_info *);
bool qa_q2_player_score(qa_q2_game *, qa_actor_id, int score, int ping, qa_error *);
bool qa_q2_player_spawn(qa_q2_game *, qa_actor_id, bool restore_loadout, const qa_q2_landmark *,
                        qa_error *);
bool qa_q2_player_respawn(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_player_after_movement(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_player_end_frame(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_player_disconnect(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_player_command(qa_q2_game *, qa_actor_id, const char *, size_t, const char *const *,
                          qa_error *);
bool qa_q2_player_teleport(qa_q2_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool qa_q2_player_chase(qa_q2_game *, qa_actor_id, int direction, bool toggle, qa_error *);
bool qa_q2_player_weapon_fired(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_player_animation(qa_q2_game *, qa_actor_id, int priority, int first, int last,
                            qa_error *);
bool qa_q2_player_carry_capture(qa_q2_game *, qa_actor_id, qa_q2_player_carry *, qa_error *);
bool qa_q2_player_carry_restore(qa_q2_game *, qa_actor_id, const qa_q2_player_carry *, qa_error *);
void qa_q2_player_carry_free(qa_q2_player_carry *);
bool qa_q2_player_consumed_key(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_players_intermission(qa_q2_game *, const char *map, const qa_q2_landmark *,
                                uint32_t flags, qa_error *);
bool qa_q2_players_camera(qa_q2_game *, qa_vec3, qa_vec3, bool entering, qa_error *);
bool qa_q2_players_finish_camera(qa_q2_game *, qa_error *);
bool qa_q2_players_frame(qa_q2_game *, qa_error *);
bool qa_q2_players_in_intermission(const qa_q2_game *);
#endif
