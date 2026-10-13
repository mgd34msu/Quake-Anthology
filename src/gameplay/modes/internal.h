#ifndef QA_MODES_INTERNAL_H
#define QA_MODES_INTERNAL_H

#include "qa/modes.h"
#include "qa/modes_save.h"
#include "qa/modes_q3_session.h"
#include "qa/arena.h"
#include "qa/pool.h"
#include "qa/id_index.h"
#include "qa/q2_sound.h"
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
typedef struct mode_match_status {
    char text[64];
    uint64_t revision, emitted_revision, seconds;
    qa_mode_phase phase;
    bool initialized, visible, setup_timed, dirty;
} mode_match_status;
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
    qa_arena item_storage;
    qa_id_index item_index;
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
    mode_match_status match_status;
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
    X(CTF_MENU_GAME, "*Quake II") \
    X(CTF_MENU_TITLE, "*ThreeWave Capture the Flag") \
    X(CTF_MENU_RED, "Join Red Team") \
    X(CTF_MENU_BLUE, "Join Blue Team") \
    X(CTF_MENU_RED_MATCH, "Join Red MATCH Team") \
    X(CTF_MENU_BLUE_MATCH, "Join Blue MATCH Team") \
    X(CTF_MENU_LOCKED, "MATCH IS LOCKED") \
    X(CTF_MENU_LOCK_NOTE, "  (entry is not permitted)") \
    X(CTF_MENU_COUNT, "  (%zu players)") \
    X(CTF_MENU_CHASE, "Chase Camera") \
    X(CTF_MENU_LEAVE_CHASE, "Leave Chase Camera") \
    X(CTF_MENU_CREDITS, "Credits") \
    X(CTF_MENU_REQUEST_MATCH, "Request Match") \
    X(CTF_MENU_SETUP, "*MATCH SETUP IN PROGRESS") \
    X(CTF_MENU_STARTING, "*MATCH STARTING") \
    X(CTF_MENU_PLAYING, "*MATCH IN PROGRESS") \
    X(CTF_MENU_CURSOR, "Use [ and ] to move cursor") \
    X(CTF_MENU_SELECT, "ENTER to select") \
    X(CTF_MENU_EXIT, "ESC to Exit Menu") \
    X(CTF_MENU_TAB, "(TAB to Return)") \
    X(CTF_MENU_VERSION, "v1.09b") \
    X(CTF_MENU_NO_CHASE, "No one to chase") \
    X(CTF_MENU_BACK, "Return to Main Menu") \
    X(CTF_MENU_PROGRAMMING, "*Programming") \
    X(CTF_MENU_ZOID, "Dave 'Zoid' Kirsch") \
    X(CTF_MENU_LEVEL_DESIGN, "*Level Design") \
    X(CTF_MENU_ANTKOW, "Christian Antkow") \
    X(CTF_MENU_WILLITS, "Tim Willits") \
    X(CTF_MENU_ART, "*Art") \
    X(CTF_MENU_CARMACK_STEED, "Adrian Carmack Paul Steed") \
    X(CTF_MENU_CLOUD, "Kevin Cloud") \
    X(CTF_MENU_SOUND, "*Sound") \
    X(CTF_MENU_KLOK, "Tom 'Bjorn' Klok") \
    X(CTF_MENU_ORIGINAL_ART, "*Original CTF Art Design") \
    X(CTF_MENU_COZZENS, "Brian 'Whaleboy' Cozzens") \
    X(CVAR_COMPETITION, "competition") \
    X(CTF_ELECTION_DISABLED, "Elections are disabled, only an admin can process this action.\n") \
    X(CTF_ELECTION_RUNNING, "Election already in progress.\n") \
    X(CTF_ELECTION_NEEDS_PLAYERS, "Not enough players for election.\n") \
    X(CTF_ELECTION_MATCH_REQUEST, "{0} has requested to switch to competition mode.\n") \
    X(CTF_ELECTION_ADMIN_REQUEST, "{0} has requested admin rights.\n") \
    X(CTF_ELECTION_MAP_REQUEST, "{0} has requested warping to level {1}.\n") \
    X(CTF_ELECTION_DIRECTIONS, "Type YES or NO to vote on this request.\n") \
    X(CTF_ELECTION_TALLY, "Votes: {0}  Needed: {1}  Time left: {2}s\n") \
    X(CTF_ELECTION_NONE, "No election is in progress.\n") \
    X(CTF_ELECTION_ALREADY_VOTED, "You already voted.\n") \
    X(CTF_ELECTION_SELF_VOTE, "You can't vote for yourself.\n") \
    X(CTF_ELECTION_TIMEOUT, "Election timed out and has been cancelled.\n") \
    X(CTF_WARPING, "{0} is warping to level {1}.\n") \
    X(CTF_ADMIN_GRANTED, "{0} has become an admin.\n") \
    X(CTF_ADMIN_HELP, "Type 'admin' to access the adminstration menu.\n") \
    X(CTF_WARP_USAGE, "Where do you want to warp to?\n") \
    X(CTF_WARP_LEVELS, "Available levels are: {0}\n") \
    X(CTF_WARP_UNKNOWN, "Unknown CTF level.\n") \
    X(CTF_PICK_TEAM, "Pick a team first (hit <TAB> for menu)\n") \
    X(CTF_ID_DISABLED, "Disabling player identication display.\n") \
    X(CTF_ID_ENABLED, "Activating player identication display.\n") \
    X(CTF_TEAM_JOINED, "{0} joined the {1} team.\n") \
    X(CTF_TEAM_CHANGED, "{0} changed to the {1} team.\n") \
    X(CTF_TEAM_ALREADY, "You are already on the {0} team.\n") \
    X(CTF_TEAM_MATCH_DENIED, "Can't change teams in a match.\n") \
    X(CTF_READY_PROMPT, "***********************\nType \"ready\" in console\nto ready up.\n***********************") \
    X(CTF_NOT_SETUP, "A match is not being setup.\n") \
    X(CTF_ALREADY_READY, "You have already commited.\n") \
    X(CTF_NOT_READY, "You haven't commited.\n") \
    X(CTF_PLAYER_READY, "{0} is ready.\n") \
    X(CTF_PLAYER_NOT_READY, "{0} is no longer ready.\n") \
    X(CTF_ALL_READY, "All players have commited.  Match starting\n") \
    X(CTF_MATCH_HALTED, "Match halted.\n") \
    X(CTF_MATCH_STARTED, "******************\n\nMATCH HAS STARTED!\n\n******************") \
    X(CTF_GHOST_CODE, "Your ghost code is **** {0} ****\n") \
    X(CTF_GHOST_HELP, "If you lose connection, you can rejoin with your score intact by typing \"ghost {0}\".\n") \
    X(CTF_GHOST_USAGE, "Usage:  ghost <code>\n") \
    X(CTF_ALREADY_PLAYING, "You are already in the game.\n") \
    X(CTF_NO_MATCH, "No match is in progress.\n") \
    X(CTF_GHOST_ACCEPTED, "Ghost code accepted, your position has been reinstated.\n") \
    X(CTF_GHOST_REINSTATED, "{0} has been reinstated to {1} team.\n") \
    X(CTF_GHOST_INVALID, "Invalid ghost code.\n") \
    X(CTF_MATCH_COMPLETED, "MATCH COMPLETED!\n") \
    X(CTF_TEAM_RESULT, "{0} TEAM:  {1} captures, {2} points\n") \
    X(CTF_WON_CAPTURES, "{0} team won over the {1} team by {2} CAPTURES!\n") \
    X(CTF_WON_POINTS, "{0} team won over the {1} team by {2} POINTS!\n") \
    X(CTF_TIED, "TIE GAME!\n") \
    X(CTF_CAPTURELIMIT, "Capturelimit hit.\n") \
    X(CHAT_TEAM, "({0}): {1}\n") \
    X(ITEM_QUAD, "item_quad") \
    X(ITEM_INVULNERABILITY, "item_invulnerability") \
    X(WEAPON_BFG, "weapon_bfg") \
    X(WEAPON_RAILGUN, "weapon_railgun") \
    X(WEAPON_ROCKETLAUNCHER, "weapon_rocketlauncher") \
    X(WEAPON_HYPERBLASTER, "weapon_hyperblaster") \
    X(WEAPON_CHAINGUN, "weapon_chaingun") \
    X(WEAPON_GRENADELAUNCHER, "weapon_grenadelauncher") \
    X(WEAPON_MACHINEGUN, "weapon_machinegun") \
    X(WEAPON_SUPERSHOTGUN, "weapon_supershotgun") \
    X(WEAPON_SHOTGUN, "weapon_shotgun") \
    X(ITEM_POWER_SCREEN, "item_power_screen") \
    X(ITEM_POWER_SHIELD, "item_power_shield") \
    X(ITEM_ARMOR_BODY, "item_armor_body") \
    X(ITEM_ARMOR_COMBAT, "item_armor_combat") \
    X(ITEM_ARMOR_JACKET, "item_armor_jacket") \
    X(ITEM_SILENCER, "item_silencer") \
    X(ITEM_BREATHER, "item_breather") \
    X(ITEM_ENVIRO, "item_enviro") \
    X(ITEM_ADRENALINE, "item_adrenaline") \
    X(ITEM_BANDOLIER, "item_bandolier") \
    X(ITEM_PACK, "item_pack") \
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
    X(Q2_TIMELIMIT_HIT, "Timelimit hit.\n") \
    X(Q2_FRAGLIMIT_HIT, "Fraglimit hit.\n") \
    X(Q2_RR_TIMELIMIT_HIT, "$g_timelimit_hit") \
    X(Q2_RR_FRAGLIMIT_HIT, "$g_fraglimit_hit") \
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
    QA_MODE_Q2_HUD_IMAGE_NAMES(X) \
    X(CTF_STATUSBAR, NULL) \
    X(CTF_SCORE_HEADER, "if 24 xv 8 yv 8 pic 24 endif xv 40 yv 28 string \"%4d/%-3d\" xv 98 yv 12 num 2 18 if 25 xv 168 yv 8 pic 25 endif xv 200 yv 28 string \"%4d/%-3d\" xv 256 yv 12 num 2 20 ") \
    X(CTF_SCORE_SPECTATORS, "Spectators") \
    X(CTF_SCORE_MORE, "..and %d more") \
    X(PIC_SBFCTF1, "sbfctf1") \
    X(PIC_SBFCTF2, "sbfctf2") \
    X(PIC_A_REDFLAG, "a_redflag") \
    X(PIC_A_STRENGTH, "a_strength") \
    X(PIC_A_HASTE, "a_haste") \
    X(PIC_A_RESIST, "a_resist") \
    X(PIC_A_REGEN, "a_regen") \
    X(PIC_K_REDKEY, "k_redkey") \
    X(PIC_I_TAGTOKEN, "i_tagtoken") \
    X(QC_ALREADY_HAVE_RUNE, "$qc_already_have_rune") \
    X(CTF_ALREADY_TECH, "You already have a TECH powerup.") \
    X(CTF_RED, "RED") \
    X(CTF_BLUE, "BLUE") \
    X(CTF_UNKNOWN, "UKNOWN") \
    X(CTF_FLAG_TAKEN, "{0} got the {1} flag!\n") \
    X(CTF_FLAG_RETURNED, "{0} returned the {1} flag!\n") \
    X(CTF_FLAG_CAPTURED, "{0} captured the {1} flag!\n") \
    X(CTF_FLAG_AUTO_RETURN, "The {0} flag has returned!\n") \
    X(CTF_RETURN_ASSIST, "{0} gets an assist for returning the flag!\n") \
    X(CTF_FRAG_ASSIST, "{0} gets an assist for fragging the flag carrier!\n") \
    X(SOUND_CTF_FLAGTK_WAV, "ctf/flagtk.wav") \
    X(SOUND_CTF_FLAGRET_WAV, "ctf/flagret.wav") \
    X(SOUND_CTF_FLAGCAP_WAV, "ctf/flagcap.wav") \
    X(LMCTF_RED, "red") \
    X(LMCTF_BLUE, "blue") \
    X(LMCTF_UNKNOWN, "unknown") \
    X(LMCTF_FLAG_TAKEN, "{0} stole the {1} flag.\n") \
    X(LMCTF_YOUR_FLAG_TAKEN, "{0} stole your flag!\n") \
    X(LMCTF_FLAG_RETURNED, "{0} returned the {1} flag.\n") \
    X(LMCTF_YOUR_FLAG_RETURNED, "{0} returned your flag!\n") \
    X(LMCTF_FLAG_CAPTURED, "{0} captured the {1} flag.\n") \
    X(LMCTF_YOUR_FLAG_CAPTURED, "{0} captured your flag!\n") \
    X(LMCTF_RETURN_ASSIST, "{0} helped {1} return the {2} flag.\n") \
    X(LMCTF_CAPTURE_FRAG_ASSIST, "{0} assisted the capture by killing the flag carrier.\n") \
    X(LMCTF_CAPTURE_RETURN_ASSIST, "{0} assisted the capture by returning the flag.\n") \
    X(LMCTF_CAPTURE_DEFEND_ASSIST, "{0} assisted the capture by defending the flag.\n") \
    X(LMCTF_FLAG_DROPPED, "{0} lost the {1} flag.\n") \
    X(LMCTF_DEFEND_FLAG, "{0} defends the {1} flag.\n") \
    X(LMCTF_DEFEND_BASE, "{0} defends the {1} base.\n") \
    X(LMCTF_DEFEND_AGGRESSIVE_CARRIER, "{0} defends the {1} flag carrier from an aggressive enemy.\n") \
    X(LMCTF_DEFEND_CARRIER, "{0} defends the {1} flag carrier.\n") \
    X(LMCTF_KILL_CARRIER, "{0} killed the enemy flag carrier.\n") \
    X(LMCTF_MATCH_COUNTDOWN, "{0} seconds until match begins.\n") \
    X(LMCTF_MATCH_MINUTES, "{0} minutes until match ends.\n") \
    X(LMCTF_MATCH_MINUTE, "{0} minute until match ends.\n") \
    X(LMCTF_MATCH_RED_WINS, "Red: {0} beats blue: {1}!\n") \
    X(LMCTF_MATCH_BLUE_WINS, "Blue: {0} beats red: {1}!\n") \
    X(LMCTF_MATCH_TIED, "Tie game at {0}!\n") \
    X(LMCTF_RCON_OFF, "Rcon Mode is off\n") \
    X(LMCTF_RCON_GRANTED, "You are now an Rcon\n") \
    X(LMCTF_REFEREE_OFF, "Referee Mode is off\n") \
    X(LMCTF_REFEREE_GRANTED, "You are now a Referee\n") \
    X(LMCTF_REFEREE_BAD_PASSWORD, "Incorrect Referee Password\n") \
    X(LMCTF_LOCK_DENIED, "Only referees can (un)lock teams.\n") \
    X(LMCTF_ADMIN_DENIED, "Referee-only command denied.\n") \
    X(LMCTF_MATCH_RUNNING, "Match already running, stop it first\n") \
    X(LMCTF_MATCH_NOT_RUNNING, "No match running\n") \
    X(LMCTF_MATCH_STOPPED, "Match stopped by {0}\n") \
    X(LMCTF_TEAMS_LOCKED, "Teams are now locked\n") \
    X(LMCTF_TEAMS_UNLOCKED, "Teams are now unlocked\n") \
    X(LMCTF_GAME_PAUSED, "Game Paused\n") \
    X(LMCTF_GAME_UNPAUSED, "Game Unpaused\n") \
    X(LMCTF_VOTE_NEEDS_PLAYERS, "You need at least four players on the server to initiate a vote\n") \
    X(LMCTF_VOTE_ALREADY_STARTED, "Vote has already been started\n") \
    X(LMCTF_VOTE_SKIP_STARTED, "{0} started vote to skip level.\n") \
    X(LMCTF_VOTE_NOT_STARTED, "A vote has not been initiated.\n") \
    X(LMCTF_VOTE_YES, "You have voted YES\n") \
    X(LMCTF_VOTE_NO, "You have voted NO\n") \
    X(LMCTF_VOTE_ENDED, "Vote session has ended\n") \
    X(LMCTF_VOTE_RESULT, "VOTE RESULT: YES:{0}  NO:{1}  Abstained:{2}\n") \
    X(LMCTF_VOTE_NEEDS_BALLOTS, "Vote Fails: you need at least 2 ballots cast!\n") \
    X(LMCTF_VOTE_SKIP_PASSED, "Vote to skip level Passes with {0} percent majority\n") \
    X(LMCTF_VOTE_FAILED, "Vote Fails with {0} percent majority\n") \
    X(LMCTF_TEAM_SWITCH_OFF, "Sorry.  Team switching has been turned\n off on this server.\n") \
    X(LMCTF_TEAM_LOCKED, "Teams are currently locked.\n") \
    X(LMCTF_CURRENT_TEAM, "You are currently on the {0} team.\n") \
    X(LMCTF_CURRENT_TEAM_NUMBER, "You are currently team {0}.\n") \
    X(LMCTF_TEAM_USAGE, "Use 'team red' or 'team blue' to change teams.\n") \
    X(LMCTF_TEAM_CHANGED, "{0} is now on the {1} team.\n") \
    X(SOUND_MISC_SECRET_WAV, QA_Q2_SOUND_MISC_SECRET) \
    X(SOUND_CTF_R_RETURNED_WAV, "ctf/r_returned.wav") \
    X(SOUND_CTF_B_RETURNED_WAV, "ctf/b_returned.wav") \
    X(SOUND_CTF_R_STOLEN_WAV, "ctf/r_stolen.wav") \
    X(SOUND_CTF_B_STOLEN_WAV, "ctf/b_stolen.wav") \
    X(RESOURCE_Q2_BFG_EXPLOSION, "q2:bfg-explosion") \
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
    X(Q2_ITEM_TECH5, "q2:item_tech5") \
    X(SOUND_CTF_REDSCORE1, "ctf/redscore1.wav") \
    X(SOUND_CTF_REDSCORE2, "ctf/redscore2.wav") \
    X(SOUND_CTF_REDSCORE3, "ctf/redscore3.wav") \
    X(SOUND_CTF_REDSCORE4, "ctf/redscore4.wav") \
    X(SOUND_CTF_BLUESCORE1, "ctf/bluescore1.wav") \
    X(SOUND_CTF_BLUESCORE2, "ctf/bluescore2.wav") \
    X(SOUND_CTF_BLUESCORE3, "ctf/bluescore3.wav") \
    X(SOUND_CTF_BLUESCORE4, "ctf/bluescore4.wav")

typedef enum mode_name {
#define MODE_NAME_ENUM(key, text) MODE_NAME_##key,
    MODE_NAME_LIST(MODE_NAME_ENUM)
#undef MODE_NAME_ENUM
    MODE_NAME_COUNT
} mode_name;

typedef struct mode_item_scope {
    qa_arena storage;
} mode_item_scope;

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
    qa_arena item_scope_storage;
    qa_pool item_scopes;
    size_t item_scope_bytes;
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
bool mode_ctf_reset(qa_modes *, mode_instance *, bool setup, qa_error *);
bool mode_ctf_admin_grant(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_ctf_named_message(qa_modes *, mode_instance *, qa_actor_id subject,
    mode_name, qa_string_id map, int32_t level, qa_error *);
bool mode_alive(qa_modes *, qa_actor_id);
bool mode_sound(qa_modes *, mode_instance *, qa_actor_id, qa_string_id, float, qa_error *);
bool mode_sound_channel(qa_modes *, mode_instance *, qa_actor_id, qa_string_id, int32_t,
                        float, qa_error *);
bool mode_sound_full(qa_modes *, mode_instance *, qa_actor_id, qa_string_id, int32_t,
                    float, float, qa_error *);
bool mode_builtin_emit(qa_modes *, mode_instance *, const qa_builtin_event *, qa_error *);
bool mode_message(qa_modes *, mode_instance *, qa_actor_id, qa_string_id,
                   const qa_builtin_message_arg *, size_t, qa_error *);
bool mode_message_level(qa_modes *, mode_instance *, qa_actor_id, qa_string_id,
                        const qa_builtin_message_arg *, size_t, int32_t, qa_error *);
bool mode_count(qa_modes *, qa_actor_id, qa_item_id, double *, qa_error *);
bool mode_set_count(qa_modes *, qa_actor_id, qa_item_id, double, qa_error *);
bool mode_object_count(qa_modes *, mode_instance *, mode_object *, qa_actor_id, double, qa_error *);
bool mode_inventory_item(qa_modes *, mode_instance *, qa_item_id, qa_item_id *, qa_error *);
bool mode_items_reserve(mode_instance *, size_t, qa_error *);
void mode_items_reindex(mode_instance *);
bool mode_item_scopes_prepare(qa_modes *, qa_error *);
void mode_item_scopes_destroy(qa_modes *);
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
bool mode_flag_drop_announce(qa_modes *, mode_instance *, mode_object *, qa_actor_id, qa_error *);
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
bool mode_ui_command(qa_modes *, mode_instance *, qa_actor_id,
                     const qa_command_invocation *, bool *, qa_error *);
bool mode_ui_close(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_ui_open_join(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_ui_scoreboard(qa_modes *, mode_instance *, qa_actor_id, bool visible, qa_error *);
bool mode_ui_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_ui_request_team(qa_modes *, mode_instance *, qa_actor_id, qa_team_id,
                         bool *, qa_error *);
bool mode_ctf_ghost_assign(qa_modes *, mode_instance *, mode_member *, qa_error *);
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
