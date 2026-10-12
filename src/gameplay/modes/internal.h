#ifndef QA_MODES_INTERNAL_H
#define QA_MODES_INTERNAL_H

#include "qa/modes.h"
#include "qa/modes_save.h"
#include "qa/modes_q3_session.h"
#include "qa/arena.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MODE_SECOND UINT64_C(1000000000)
#define MODE_MILLISECOND UINT64_C(1000000)

typedef struct mode_player {
    qa_modes *modes;
    qa_actor_id actor;
    qa_actor_player *identity;
    bool connected, connecting;
    qa_inventory_lease items;
    bool active;
} mode_player;
static inline qa_match_player mode_player_connection(const mode_player *player) {
    return (qa_match_player){.actor = player->actor, .name = player->identity->name,
        .connected = player->connected, .connecting = player->connecting,
        .bot = player->identity->bot};
}
typedef qa_mode_member_state mode_member;
typedef qa_mode_ghost_state mode_ghost;
typedef struct mode_match_owner {
    qa_match_binding binding;
    uint64_t serial;
} mode_match_owner;
typedef struct mode_rank_entry {
    qa_actor_id actor;
    int32_t score;
    uint64_t spectator_since;
    int32_t q3_spectator_time;
    bool q3_source;
    uint32_t order;
    uint8_t group;
} mode_rank_entry;
typedef struct mode_instance {
    qa_mode_id id;
    qa_mode_view value;
    mode_member *members;
    mode_match_owner *bindings;
    mode_ghost *ghosts;
    qa_actor_id *sorted;
    mode_rank_entry *ranks;
    qa_mode_spawnpoint *spawns;
    size_t spawn_count;
    qa_mode_item_binding *items;
    size_t item_count, item_capacity;
    qa_actor_id bases[3], ball, tag, tag_owner, last_ball_touch;
    bool base_admitting[3];
    qa_actor_id last_spawns[4];
    qa_actor_id rogue_spawn_spot;
    qa_mode_vote votes[4];
    uint64_t vote_started[4], ready_since_ns, next_second_ns;
    uint64_t flag_sound_ns[3], attack_sound_ns[3];
    uint64_t relic_spawn_ns, team_location_ns;
    size_t rune_cursor;
    int32_t next_location;
    bool relics_started, rune_forward;
    uint64_t serial;
    int32_t tag_count, remaining_seconds;
    bool active, countdown_announced, restart_sent;
    qa_mode_q3_settings q3_settings;
    int32_t q3_started_ms, q3_warmup_ms;
    uint64_t q3_warmup_seen;
    bool q3_settings_present;
    void *horde;
} mode_instance;
typedef struct mode_object {
    qa_modes *modes;
    qa_actor_id actor;
    qa_mode_id mode;
    qa_mode_object_spec spec;
    qa_mode_object_view value;
    qa_vec3 home;
    qa_actor_id base, dropped_actor;
    qa_actor_collision collision;
    qa_physics_properties physics;
    qa_objective_lease objective;
    uint64_t next_ns, owner_until_ns, animation_ns, expire_ns, born_ns;
    int32_t tag_stage;
    bool active, admitting, targets_used, has_physics, dropped, global_animation, q3_source_owned;
} mode_object;
typedef struct mode_objective {
    qa_objective_binding binding;
    uint64_t serial;
    bool active, reserved;
} mode_objective;
#define MODE_NAME_LIST(X) \
    X(SOUND_WEAPONS_PKUP_WAV, "weapons/pkup.wav") \
    X(SOUND_RUNES_END1_WAV, "runes/end1.wav") \
    X(SOUND_RUNES_END2_WAV, "runes/end2.wav") \
    X(SOUND_RUNES_END3_WAV, "runes/end3.wav") \
    X(SOUND_RUNE_RUNE1_WAV, "rune/rune1.wav") \
    X(SOUND_RUNE_RUNE22_WAV, "rune/rune22.wav") \
    X(SOUND_RUNE_RUNE2_WAV, "rune/rune2.wav") \
    X(SOUND_RUNE_RUNE3_WAV, "rune/rune3.wav") \
    X(SOUND_RUNE_RUNE4_WAV, "rune/rune4.wav") \
    X(SOUND_CTF_TECH2X_WAV, "ctf/tech2x.wav") \
    X(SOUND_CTF_TECH2_WAV, "ctf/tech2.wav") \
    X(SOUND_CTF_TECH3_WAV, "ctf/tech3.wav") \
    X(SOUND_CTF_TECH4_WAV, "ctf/tech4.wav") \
    X(SOUND_CTF_TECH1_WAV, "ctf/tech1.wav") \
    X(SOUND_PLAYER_LAVA1_WAV, "player/lava1.wav") \
    X(SOUND_CTF_STRENGTH_WAV, "ctf/strength.wav") \
    X(SOUND_CTF_REGEN_WAV, "ctf/regen.wav") \
    X(SOUND_CTF_RESIST_WAV, "ctf/resist.wav") \
    X(SOUND_BRAIN_BRNATCK3_WAV, "brain/brnatck3.wav") \
    X(SOUND_RUNES_END4_WAV, "runes/end4.wav") \
    X(SOUND_MISC_AM_PKUP_WAV, "misc/am_pkup.wav") \
    X(SOUND_ITEMS_PKUP_WAV, "items/pkup.wav") \
    X(CTF_TEAMCHANGE, "ctf:teamchange") \
    X(CTF_REFLECTION, "ctf:reflection") \
    X(MAP_RESTART, "map_restart 0\n") \
    X(KEY_GOLD, "q1:key/gold") \
    X(KEY_SILVER, "q1:key/silver") \
    X(MONSTER_ARMY, "monster_army") \
    X(MONSTER_DOG, "monster_dog") \
    X(MONSTER_ENFORCER, "monster_enforcer") \
    X(MONSTER_OGRE, "monster_ogre") \
    X(MONSTER_KNIGHT, "monster_knight") \
    X(MONSTER_ZOMBIE, "monster_zombie") \
    X(MONSTER_WIZARD, "monster_wizard") \
    X(MONSTER_HELL_KNIGHT, "monster_hell_knight") \
    X(MONSTER_SHAMBLER, "monster_shambler") \
    X(MONSTER_DEMON1, "monster_demon1") \
    X(MONSTER_SHALRATH, "monster_shalrath") \
    X(ITEM_HEALTH, "item_health") \
    X(ITEM_HEALTH_SMALL, "item_health_small") \
    X(ITEM_HEALTH_LARGE, "item_health_large") \
    X(ITEM_SHELLS, "item_shells") \
    X(ITEM_SPIKES, "item_spikes") \
    X(ITEM_ROCKETS, "item_rockets") \
    X(ITEM_CELLS, "item_cells") \
    X(ITEM_ARMOR1, "item_armor1") \
    X(ITEM_KEY1, "item_key1") \
    X(ITEM_KEY2, "item_key2") \
    X(ITEM_ARTIFACT_INVULNERABILITY, "item_artifact_invulnerability") \
    X(ITEM_ARTIFACT_SUPER_DAMAGE, "item_artifact_super_damage") \
    X(Q1_AMMO_SHELLS, "q1:ammo/shells") \
    X(Q1_AMMO_NAILS, "q1:ammo/nails") \
    X(Q1_AMMO_ROCKETS, "q1:ammo/rockets") \
    X(Q1_AMMO_CELLS, "q1:ammo/cells") \
    X(Q1_ITEM_ARMOR1, "q1:item_armor1") \
    X(Q2_ITEM_ARMOR_JACKET, "q2:item_armor_jacket") \
    X(ITEM_FLAG_TEAM1, "item_flag_team1") \
    X(ITEM_FLAG_TEAM2, "item_flag_team2") \
    X(ITEM_TECH1, "item_tech1") \
    X(ITEM_TECH2, "item_tech2") \
    X(ITEM_TECH3, "item_tech3") \
    X(ITEM_TECH4, "item_tech4") \
    X(FLAG, "flag") \
    X(DAMAGE_RUNE, "damage_rune") \
    X(HASTE_RUNE, "haste_rune") \
    X(RESIST_RUNE, "resist_rune") \
    X(REGEN_RUNE, "regen_rune") \
    X(VAMPIRE_RUNE, "vampire_rune") \
    X(DM_TAG_TOKEN, "dm_tag_token") \
    X(Q2_ITEM_FLAG_TEAM1, "q2:item_flag_team1") \
    X(Q2_ITEM_FLAG_TEAM2, "q2:item_flag_team2") \
    X(Q2_ITEM_TECH1, "q2:item_tech1") \
    X(Q2_ITEM_TECH2, "q2:item_tech2") \
    X(Q2_ITEM_TECH3, "q2:item_tech3") \
    X(Q2_ITEM_TECH4, "q2:item_tech4") \
    X(Q2_FLAG, "q2:flag") \
    X(Q2_DAMAGE_RUNE, "q2:damage_rune") \
    X(Q2_HASTE_RUNE, "q2:haste_rune") \
    X(Q2_RESIST_RUNE, "q2:resist_rune") \
    X(Q2_REGEN_RUNE, "q2:regen_rune") \
    X(Q2_VAMPIRE_RUNE, "q2:vampire_rune") \
    X(Q2_DM_TAG_TOKEN, "q2:dm_tag_token") \
    X(LABEL_RED_FLAG, "Red Flag") \
    X(LABEL_BLUE_FLAG, "Blue Flag") \
    X(LABEL_DISRUPTOR_SHIELD, "Disruptor Shield") \
    X(LABEL_POWER_AMPLIFIER, "Power Amplifier") \
    X(LABEL_TIME_ACCEL, "Time Accel") \
    X(LABEL_AUTODOC, "AutoDoc") \
    X(LABEL_ENEMY_FLAG, "Enemy Flag") \
    X(LABEL_DAMAGE_ARTIFACT, "Damage Artifact") \
    X(LABEL_HASTE_ARTIFACT, "Haste Artifact") \
    X(LABEL_RESIST_ARTIFACT, "Resist Artifact") \
    X(LABEL_REGEN_ARTIFACT, "Regen Artifact") \
    X(LABEL_VAMPIRE_ARTIFACT, "Vampire Artifact") \
    X(LABEL_TAG_TOKEN, "Tag Token") \
    X(PIC_I_CTF1, "i_ctf1") \
    X(PIC_I_CTF2, "i_ctf2") \
    X(PIC_TECH1, "tech1") \
    X(PIC_TECH2, "tech2") \
    X(PIC_TECH3, "tech3") \
    X(PIC_TECH4, "tech4") \
    X(PIC_A_REDFLAG, "a_redflag") \
    X(PIC_A_STRENGTH, "a_strength") \
    X(PIC_A_HASTE, "a_haste") \
    X(PIC_A_RESIST, "a_resist") \
    X(PIC_A_REGEN, "a_regen") \
    X(PIC_K_REDKEY, "k_redkey") \
    X(PIC_I_TAGTOKEN, "i_tagtoken") \
    X(QC_ALREADY_HAVE_RUNE, "$qc_already_have_rune") \
    X(QC_RUNE_RESISTANCE, "$qc_rune_resistance") \
    X(QC_RUNE_STRENGTH, "$qc_rune_strength") \
    X(QC_RUNE_HASTE, "$qc_rune_haste") \
    X(QC_RUNE_REGENERATION, "$qc_rune_regeneration") \
    X(QC_GOT_TOKEN, "$qc_got_token") \
    X(QC_LOST_TOKEN, "$qc_lost_token") \
    X(QC_HAS_TOKEN, "$qc_has_token") \
    X(QC_GOT_QUAD, "$qc_got_quad") \
    X(BODYQUE, "bodyque") \
    X(INFO_PLAYER_DEATHMATCH, "info_player_deathmatch") \
    X(INFO_PLAYER_START, "info_player_start") \
    X(INFO_PLAYER_TEAM1, "info_player_team1") \
    X(INFO_PLAYER_TEAM2, "info_player_team2") \
    X(INFO_VOTE_DESTINATION, "info_vote_destination") \
    X(TESTPLAYERSTART, "testplayerstart") \
    X(TEAM_CTF_REDPLAYER, "team_CTF_redplayer") \
    X(TEAM_CTF_REDSPAWN, "team_CTF_redspawn") \
    X(TEAM_CTF_BLUEPLAYER, "team_CTF_blueplayer") \
    X(TEAM_CTF_BLUESPAWN, "team_CTF_bluespawn") \
    X(FALLING, "falling") \
    X(Q1_WEAPON_AXE, "q1:weapon/axe") \
    X(Q1_WEAPON_SHOTGUN, "q1:weapon/shotgun") \
    X(Q1_WEAPON_SUPERSHOTGUN, "q1:weapon/supershotgun") \
    X(Q1_WEAPON_GRENADELAUNCHER, "q1:weapon/grenadelauncher") \
    X(Q1_WEAPON_ROCKETLAUNCHER, "q1:weapon/rocketlauncher") \
    X(Q1_WEAPON_MULTI_GRENADE, "q1:weapon/rogue:multi-grenade") \
    X(Q1_WEAPON_MULTI_ROCKET, "q1:weapon/rogue:multi-rocket") \
    X(Q1_WEAPON_PLASMA, "q1:weapon/rogue:plasma") \
    X(DM_DBALL_BALL_START, "dm_dball_ball_start") \
    X(DM_DBALL_TEAM1_START, "dm_dball_team1_start") \
    X(DM_DBALL_TEAM2_START, "dm_dball_team2_start") \
    X(CTF_TEAMKILL, "ctf:teamkill") \
    X(ANTHOLOGY_MODE_OBJECT, "anthology:mode-object") \
    X(ANTHOLOGY_DROPPED_FLAG, "anthology:dropped-flag") \
    X(HORDE_EVENT, "horde_event") \
    X(MODEL_PROGS_CTFBASE_MDL, "progs/ctfbase.mdl") \
    X(MODEL_PROGS_CTFMODEL_MDL, "progs/ctfmodel.mdl") \
    X(MODEL_PROGS_FLAG_MDL, "progs/flag.mdl") \
    X(MODEL_PLAYERS_MALE_FLAG1_MD2, "players/male/flag1.md2") \
    X(MODEL_PLAYERS_MALE_FLAG2_MD2, "players/male/flag2.md2") \
    X(MODEL_FLAGS_R_FLAG_MD3, "models/flags/r_flag.md3") \
    X(MODEL_FLAGS_B_FLAG_MD3, "models/flags/b_flag.md3") \
    X(MODEL_FLAGS_N_FLAG_MD3, "models/flags/n_flag.md3") \
    X(MODEL_PROGS_END1_MDL, "progs/end1.mdl") \
    X(MODEL_PROGS_END2_MDL, "progs/end2.mdl") \
    X(MODEL_PROGS_END3_MDL, "progs/end3.mdl") \
    X(MODEL_PROGS_END4_MDL, "progs/end4.mdl") \
    X(MODEL_CTF_RESISTANCE_MD2, "models/ctf/resistance/tris.md2") \
    X(MODEL_CTF_STRENGTH_MD2, "models/ctf/strength/tris.md2") \
    X(MODEL_CTF_HASTE_MD2, "models/ctf/haste/tris.md2") \
    X(MODEL_CTF_REGENERATION_MD2, "models/ctf/regeneration/tris.md2") \
    X(MODEL_CTF_VAMPIRE_MD2, "models/ctf/vampire/tris.md2") \
    X(MODEL_CTF_RESIST_MD2, "models/ctf/resist/tris.md2") \
    X(MODEL_CTF_DAMAGE_MD2, "models/ctf/damage/tris.md2") \
    X(MODEL_CTF_REGEN_MD2, "models/ctf/regen/tris.md2") \
    X(MODEL_PROGS_SPHERE_MDL, "progs/sphere.mdl") \
    X(MODEL_ITEMS_TAGTOKEN_MD2, "models/items/tagtoken/tris.md2") \
    X(MODEL_OBJECTS_DBALL_MD2, "models/objects/dball/tris.md2") \
    X(MODEL_POWERUPS_R_ORB_MD3, "models/powerups/orb/r_orb.md3") \
    X(MODEL_POWERUPS_B_ORB_MD3, "models/powerups/orb/b_orb.md3") \
    X(MODEL_POWERUPS_OVERLOAD_BASE_MD3, "models/powerups/overload_base.md3") \
    X(Q1_CTF_FLAG_RED, "q1:ctf/flag/red") \
    X(Q1_CTF_FLAG_BLUE, "q1:ctf/flag/blue") \
    X(Q1_CTF_FLAG_NEUTRAL, "q1:ctf/flag/neutral") \
    X(Q3_ITEM_TEAM_CTF_REDFLAG, "q3:item/team_CTF_redflag") \
    X(Q3_ITEM_TEAM_CTF_BLUEFLAG, "q3:item/team_CTF_blueflag") \
    X(Q3_ITEM_TEAM_CTF_NEUTRALFLAG, "q3:item/team_CTF_neutralflag") \
    X(Q1_ROGUE_TAG, "q1:rogue/tag") \
    X(Q1_CTF_RUNE_RESISTANCE, "q1:ctf/rune/resistance") \
    X(Q1_CTF_RUNE_STRENGTH, "q1:ctf/rune/strength") \
    X(Q1_CTF_RUNE_HASTE, "q1:ctf/rune/haste") \
    X(Q1_CTF_RUNE_REGENERATION, "q1:ctf/rune/regeneration") \
    X(Q2_ITEM_TECH5, "q2:item_tech5")

typedef enum mode_name {
#define MODE_NAME_ENUM(key, text) MODE_NAME_##key,
    MODE_NAME_LIST(MODE_NAME_ENUM)
#undef MODE_NAME_ENUM
    MODE_NAME_COUNT
} mode_name;

struct qa_modes {
    qa_modes_options options;
    qa_string_id runtime_names[MODE_NAME_COUNT];
    uint32_t actor_capacity, mode_capacity, objective_capacity;
    mode_player *players;
    mode_object *objects;
    mode_instance *instances;
    mode_objective *objectives;
    uint64_t next_serial, random;
    uint64_t attack_sequence;
    unsigned callback_depth;
    qa_mode_objective_checkpoint *restored_objectives;
    size_t restored_objective_count;
    bool source_restored;
    qa_arena snapshot_storage;
    qa_world_actor_snapshot players_order, observations;
};

bool mode_fail(qa_error *, const char *);
bool mode_callback_end(qa_modes *, bool);
#define MODE_CALLBACK(m, call) ((m)->callback_depth++, mode_callback_end((m), (call)))
bool mode_live(const qa_modes *, qa_actor_id);
mode_player *mode_player_get(qa_modes *, qa_actor_id);
mode_instance *mode_get(qa_modes *, qa_mode_id);
mode_member *mode_member_get(qa_modes *, mode_instance *, qa_actor_id);
mode_object *mode_object_get(qa_modes *, qa_actor_id);
int mode_team_index(const mode_instance *, qa_team_id);
int32_t mode_add_i32(int32_t, int32_t);
uint32_t mode_random(qa_modes *);
float mode_random_float(qa_modes *);
bool mode_event(qa_modes *, mode_instance *, qa_mode_event_kind, qa_actor_id, qa_actor_id,
                qa_actor_id, qa_team_id, int32_t, int32_t, qa_error *);
bool mode_intent(qa_modes *, mode_instance *, qa_match_intent_kind, qa_actor_id, qa_team_id,
                 qa_string_id, qa_error *);
bool mode_alive(qa_modes *, qa_actor_id);
bool mode_sound(qa_modes *, mode_instance *, qa_actor_id, qa_string_id, float, qa_error *);
bool mode_sound_channel(qa_modes *, mode_instance *, qa_actor_id, qa_string_id, int32_t,
                        float, qa_error *);
bool mode_count(qa_modes *, qa_actor_id, qa_item_id, double *, qa_error *);
bool mode_set_count(qa_modes *, qa_actor_id, qa_item_id, double, qa_error *);
bool mode_object_count(qa_modes *, mode_instance *, mode_object *, qa_actor_id, double, qa_error *);
bool mode_inventory_item(qa_modes *, mode_instance *, qa_item_id, qa_item_id *, qa_error *);
bool mode_reserve_objective(qa_modes *, const qa_objective_binding *, qa_objective_lease *, qa_error *);
bool mode_commit_objective(qa_modes *, qa_objective_lease);
bool mode_set_phase(qa_modes *, mode_instance *, qa_mode_phase, uint64_t, qa_error *);
bool mode_match_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
bool mode_q3_warmup_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_q3_limits(qa_modes *, mode_instance *, qa_error *);
bool mode_q3_session_initialize(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_q3_session_team(qa_modes *, mode_instance *, qa_actor_id, bool, qa_error *);
bool mode_q3_session_follow(qa_modes *, mode_instance *, qa_actor_id, qa_actor_id, int,
                            qa_error *);
bool mode_vote_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_intent_command_valid(const qa_modes *, qa_mode_source,
                               const qa_match_intent *, bool required);
bool mode_objects_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
bool mode_relic_frame(qa_modes *, mode_instance *, qa_actor_id, mode_member *, qa_error *);
bool mode_relic_source_current(qa_modes *, mode_instance *, qa_error *);
bool mode_flag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_relic_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_tag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_ball_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_ball_frame(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_ball_reset(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_flag_reset(qa_modes *, mode_instance *, mode_object *, bool, qa_error *);
bool mode_object_hide(qa_modes *, mode_object *, bool, qa_error *);
bool mode_object_drop(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool, qa_error *);
bool mode_object_relocate(qa_modes *, mode_instance *, mode_object *, bool, qa_error *);
bool mode_object_sync(qa_modes *, mode_object *, qa_error *);
bool mode_object_notify(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_flag_bonus(qa_modes *, mode_instance *, qa_actor_id, qa_actor_id, qa_error *);
bool mode_tag_death(qa_modes *, mode_instance *, const qa_damage_outcome *, bool,
                    const qa_mode_frag *, qa_error *);
bool mode_rogue_tag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *,
                          qa_error *);
bool mode_rogue_tag_frame(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_rogue_tag_drop(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_obelisk_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *,
                        qa_error *);
bool mode_horde_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
void mode_horde_free(mode_instance *);
bool mode_horde_retire(qa_modes *, mode_instance *, qa_error *);
bool mode_horde_reconcile_keys(qa_modes *, mode_instance *, qa_error *);
bool mode_horde_death(qa_modes *, mode_instance *, const qa_damage_outcome *, qa_error *);
bool mode_horde_respawn(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_near(qa_modes *, qa_actor_id, qa_actor_id, float);
bool mode_visible(qa_modes *, qa_actor_id, qa_actor_id, bool);
bool mode_rules_valid(const qa_mode_rules *);
bool mode_damage_prepare(qa_modes *, mode_instance *, qa_damage_request *, qa_error *);
bool mode_damage(qa_modes *, mode_instance *, qa_game_family, qa_damage_request *, qa_error *);
bool mode_horde_capture(qa_modes *, mode_instance *, qa_horde_checkpoint **, qa_error *);
bool mode_horde_restore(qa_modes *, mode_instance *, const qa_horde_checkpoint *, qa_error *);
void mode_horde_checkpoint_free(qa_horde_checkpoint *);
bool mode_object_bind_objective(qa_modes *, mode_object *, qa_error *);
bool mode_join(qa_modes *, mode_instance *, qa_actor_id, qa_team_id, bool, bool, qa_error *);
bool mode_relic_place(qa_modes *, mode_instance *, mode_object *, bool initial, qa_error *);
bool mode_relic_spawn_all(qa_modes *, mode_instance *, qa_error *);
bool mode_rogue_relic_drop(qa_modes *, mode_instance *, qa_actor_id, mode_member *, qa_error *);
bool mode_team_info_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_update_ghosts(qa_modes *, mode_instance *, qa_error *);
void mode_stat_add(mode_instance *, int32_t *, int32_t);
bool mode_checkpoint_restore_source(qa_modes *, const qa_modes_checkpoint *, qa_error *);
bool mode_items_reconnect(qa_modes *, qa_actor_id, qa_error *);
bool mode_q1_source_current(qa_modes *, mode_instance *, qa_actor_id,
    bool *observer, qa_error *);

#endif
