#ifndef QA_MODES_H
#define QA_MODES_H

#include "qa/builtin.h"

typedef struct qa_modes qa_modes;
typedef struct qa_command_invocation qa_command_invocation;
typedef struct qa_mode_id {
    uint32_t slot;
    uint64_t generation;
} qa_mode_id;
typedef enum qa_mode_source {
    QA_MODE_Q1,
    QA_MODE_THREEWAVE,
    QA_MODE_ROGUE,
    QA_MODE_Q1_HORDE,
    QA_MODE_Q2,
    QA_MODE_Q2_CTF,
    QA_MODE_LMCTF,
    QA_MODE_Q2_TAG,
    QA_MODE_Q2_DEATHBALL,
    QA_MODE_Q3,
    QA_MODE_TEAM_ARENA
} qa_mode_source;
typedef enum qa_mode_kind {
    QA_MODE_COOPERATIVE,
    QA_MODE_FFA,
    QA_MODE_DUEL,
    QA_MODE_SINGLE_PLAYER,
    QA_MODE_TEAM_DEATHMATCH,
    QA_MODE_CTF,
    QA_MODE_ONE_FLAG,
    QA_MODE_OVERLOAD,
    QA_MODE_HARVESTER,
    QA_MODE_TAG,
    QA_MODE_DEATHBALL,
    QA_MODE_HORDE
} qa_mode_kind;
typedef enum qa_mode_phase {
    QA_MODE_WAITING,
    QA_MODE_SETUP,
    QA_MODE_COUNTDOWN,
    QA_MODE_PLAYING,
    QA_MODE_EXIT_PENDING,
    QA_MODE_INTERMISSION,
    QA_MODE_FINISHED
} qa_mode_phase;
typedef enum qa_mode_event_kind {
    QA_MODE_SCORE,
    QA_MODE_TEAM_SCORE,
    QA_MODE_PHASE,
    QA_MODE_FLAG_TAKEN,
    QA_MODE_FLAG_RETURNED,
    QA_MODE_FLAG_CAPTURED,
    QA_MODE_RELIC_TAKEN,
    QA_MODE_OBJECTIVE_CHANGED,
    QA_MODE_OBELISK_ATTACKED,
    QA_MODE_OBELISK_REGEN,
    QA_MODE_OBELISK_PAIN,
    QA_MODE_OBELISK_EXPLODE,
    QA_MODE_HARVEST,
    QA_MODE_AWARD,
    QA_MODE_VOTE_STARTED,
    QA_MODE_VOTE_PASSED,
    QA_MODE_VOTE_FAILED,
    QA_MODE_GHOST_CODE,
    QA_MODE_HORDE_COUNTDOWN,
    QA_MODE_HORDE_WAVE,
    QA_MODE_HORDE_SPREE,
    QA_MODE_TAG_CHANGED,
    QA_MODE_BALL_GOAL,
    QA_MODE_ROSTER,
    QA_MODE_MESSAGE,
    QA_MODE_LOOT,
    QA_MODE_TEAM_INFO,
    QA_MODE_TEAM_PROMPT,
    QA_MODE_TEAM_RULES,
    QA_MODE_FLAG_STATUS,
    QA_MODE_TEAM_STANDING,
    QA_MODE_HORDE_KILL_STREAK
} qa_mode_event_kind;
typedef struct qa_mode_event {
    qa_mode_event_kind kind;
    qa_mode_id mode;
    qa_actor_id actor, other, object;
    qa_team_id team;
    qa_string_id text;
    int32_t value, detail;
    uint64_t time_ns;
} qa_mode_event;
typedef enum qa_match_intent_kind {
    QA_MATCH_NEXT_MAP,
    QA_MATCH_RESTART_MAP,
    QA_MATCH_SELECTED_MAP,
    QA_MATCH_START,
    QA_MATCH_CANCEL,
    QA_MATCH_ADMIN,
    QA_MATCH_TEAM_LEADER,
    QA_MATCH_KICK_PLAYER,
    QA_MATCH_GAME_TYPE,
    QA_MATCH_WARMUP,
    QA_MATCH_TIME_LIMIT,
    QA_MATCH_FRAG_LIMIT
} qa_match_intent_kind;
typedef struct qa_match_intent {
    qa_match_intent_kind kind;
    qa_mode_id mode;
    qa_actor_id actor;
    qa_team_id team;
    qa_string_id map;
    /* Q3 map/restart/config votes retain their bounded source command at
     * admission. Raw setter text and prior nextmap clipping remain authoritative. */
    qa_string_id source_command;
    qa_mode_kind game_type;
    float value;
} qa_match_intent;
typedef struct qa_mode_rules {
    qa_mode_source source;
    qa_mode_kind kind;
    qa_team_id teams[3];
    qa_team_id forced_team;
    int32_t frag_limit, capture_limit, warmup_seconds, competition;
    int32_t setup_seconds, countdown_seconds, match_seconds, max_game_players;
    int32_t election_percent, teamplay, rune_mask, vote_limit;
    uint32_t flags, referee_flags;
    float time_limit_minutes, obelisk_health, obelisk_regen;
    uint64_t obelisk_regen_ns, obelisk_respawn_ns;
    bool enabled, friendly_fire, force_join, match_lock, paused, auto_lock;
    bool relics, single_player_active, tournament_restart, q2_rerelease, start_map, force_balance,
        voting_disabled;
    /* Actual Rogue source deathmatch gate for damage effects. relics separately
     * admits the gamecfg bit-1 startup; carried runes survive that bit changing. */
    bool rogue_deathmatch;
} qa_mode_rules;
typedef struct qa_match_player {
    qa_actor_id actor;
    qa_string_id name;
    bool connected, connecting, bot;
} qa_match_player;
typedef struct qa_mode_player_state {
    qa_actor_id follow_target;
    qa_team_id observer_team;
    qa_team_id team;
    int32_t score, wins, losses;
    bool spectator, scoreboard, ready, leader;
    uint64_t spectator_since_ns;
    int8_t automatic_follow;
    /* Chosen Q3 rule member state. Native GAME sessions remain on their
     * physical client rows independently of this member projection. */
    int32_t q3_spectator_time_ms, q3_spectator_state, q3_spectator_client;
    int32_t ctf_last_team;
    float ctf_status, ctf_access;
} qa_mode_player_state;
typedef struct qa_mode_player_view {
    qa_mode_id mode;
    qa_match_player connection;
    qa_mode_player_state state;
} qa_mode_player_view;
/* A lease selects source-owned score/team fields for exactly one (mode, actor).
 * Reads and changes go through that owner; other mode instances never use it. */
typedef struct qa_match_binding {
    qa_actor_owner owner;
    void *context;
    bool (*score)(void *, int32_t *, qa_error *);
    bool (*set_score)(void *, int32_t, qa_error *);
    bool (*team)(void *, qa_team_id *, qa_error *);
    bool (*set_team)(void *, qa_team_id, qa_error *);
} qa_match_binding;
typedef struct qa_match_lease {
    qa_mode_id mode;
    qa_actor_id actor;
    uint64_t serial;
} qa_match_lease;
typedef struct qa_mode_statistics {
    int32_t score, kills, deaths, captures, defenses, carrier_defenses, recoveries, assists,
        assist_awards;
    int32_t rank, tokens, streak;
    int32_t q3_defend_count, q3_assist_count, q3_capture_count;
    uint64_t flag_since_ns, returned_ns, carrier_killed_ns, hurt_carrier_ns;
    uint64_t defended_ns, last_kill_ns;
    bool returned, carrier_killed, hurt_carrier, defended;
} qa_mode_statistics;
typedef struct qa_mode_view {
    qa_mode_rules rules;
    qa_mode_phase phase;
    uint64_t time_ns, started_ns, deadline_ns;
    int32_t team_scores[3], team_captures[3];
    size_t playing, voting, sorted_count;
    bool ready_exit, ctf_pregame_over;
} qa_mode_view;
typedef struct qa_mode_q3_rank_counts {
    int32_t connected, non_spectator, playing, voting, team_voting[2];
} qa_mode_q3_rank_counts;
typedef struct qa_mode_q3_settings {
    int32_t do_warmup, warmup_seconds, time_limit_minutes, frag_limit, capture_limit;
    uint64_t warmup_modification_count;
} qa_mode_q3_settings;
typedef enum qa_objective_phase {
    QA_OBJECTIVE_HOME,
    QA_OBJECTIVE_CARRIED,
    QA_OBJECTIVE_DROPPED,
    QA_OBJECTIVE_DISABLED,
    QA_OBJECTIVE_DESTROYED,
    QA_OBJECTIVE_COMPLETE
} qa_objective_phase;
typedef struct qa_objective_state {
    qa_string_id stage;
    qa_objective_phase phase;
    qa_actor_id actor, carrier, target;
    bool complete;
} qa_objective_state;
typedef struct qa_objective_binding {
    qa_mode_id mode; /* Zero generation denotes a shared campaign objective. */
    qa_actor_owner owner;
    qa_string_id id;
    bool campaign_gate, bot_goal;
    void *context;
    bool (*read)(void *, qa_objective_state *, qa_error *);
    bool (*change)(void *, const qa_objective_state *, qa_error *);
} qa_objective_binding;
typedef struct qa_objective_lease {
    uint32_t slot;
    uint64_t serial;
} qa_objective_lease;
typedef enum qa_mode_object_kind {
    QA_MODE_OBJECT_FLAG,
    QA_MODE_OBJECT_RELIC,
    QA_MODE_OBJECT_CUBE,
    QA_MODE_OBJECT_OBELISK,
    QA_MODE_OBJECT_TAG,
    QA_MODE_OBJECT_BALL,
    QA_MODE_OBJECT_GOAL,
    QA_MODE_OBJECT_SPEED,
    QA_MODE_OBJECT_LOCATION,
    QA_MODE_OBJECT_FLAG_BASE
} qa_mode_object_kind;
typedef enum qa_relic_kind {
    QA_RELIC_RESISTANCE,
    QA_RELIC_STRENGTH,
    QA_RELIC_HASTE,
    QA_RELIC_REGENERATION,
    QA_RELIC_VAMPIRE,
    QA_RELIC_COUNT
} qa_relic_kind;
typedef struct qa_mode_object_spec {
    qa_mode_object_kind kind;
    qa_team_id team;
    qa_relic_kind relic;
    qa_item_id item;
    qa_actor_id actor;
    qa_vec3 origin, angles, direction;
    qa_bounds bounds;
    qa_string_id target, id, message;
    uint32_t flags;
    int32_t location;
    float value;
    bool authored, suspended, has_bounds, retain_body;
    /* A command grant owns a carried/drop record without replacing map bases. */
    bool command_created;
} qa_mode_object_spec;
typedef struct qa_mode_object_view {
    qa_mode_id mode;
    qa_mode_object_kind kind;
    qa_team_id team;
    qa_relic_kind relic;
    qa_objective_phase phase;
    qa_actor_id carrier, previous_owner;
    qa_string_id model;
    uint64_t effects, deadline_ns;
    int32_t frame, skin, health_fraction;
    bool visible;
} qa_mode_object_view;
typedef struct qa_mode_spawnpoint {
    qa_actor_id actor;
    qa_vec3 origin, angles;
    qa_team_id team;
    qa_string_id classname;
    uint32_t flags;
    bool no_bots, no_humans;
} qa_mode_spawnpoint;
typedef struct qa_mode_loot_spawn {
    qa_string_id classname;
    qa_body_state body;
    uint32_t spawnflags;
    bool bounce;
} qa_mode_loot_spawn;
typedef struct qa_modes_hooks {
    void *context;
    bool (*event)(void *, const qa_mode_event *, qa_error *);
    bool (*intent)(void *, const qa_match_intent *, qa_error *);
    bool (*respawn)(void *, qa_mode_id, qa_actor_id, bool teleport, qa_error *);
    /* Native Q1 source clients retain their actual f32 frags. bound=false
     * delegates only modes whose physical source owns no Q1 client score. */
    bool (*q1_source_score)(void *, qa_mode_id, qa_actor_id, bool *bound, int32_t *, qa_error *);
    bool (*q1_source_set_score)(void *, qa_mode_id, qa_actor_id, int32_t, bool *bound, qa_error *);
    bool (*q1_source_add_score)(void *, qa_mode_id, qa_actor_id, int32_t, bool *bound, qa_error *);
    bool (*q1_ctf_suicide_notice)(void *, qa_mode_id, qa_actor_id, bool limited, qa_error *);
    bool (*release_grapple)(void *, qa_actor_id, qa_error *);
    bool (*intermission)(void *, qa_mode_id, qa_actor_id, qa_error *);
    bool (*spectator)(void *, qa_mode_id, qa_actor_id, bool, qa_error *);
    /* Restored source owners resolve their binding after shared actors exist.
     * Called only for saved externally owned score/team state. */
    bool (*restore_player_binding)(void *, qa_mode_id, qa_actor_id, qa_actor_owner,
                                   qa_match_binding *, qa_error *);
    bool (*visible)(void *, qa_actor_id from, qa_actor_id to, bool pvs_only);
    qa_actor_owner (*combat_provider)(void *, qa_actor_id, qa_game_family);
    bool (*grapple_pulling)(void *, qa_actor_id);
    bool (*character_frame)(void *, qa_actor_id, int32_t *);
    bool (*player_view)(void *, qa_actor_id, qa_vec3 *);
    bool (*select_weapon)(void *, qa_actor_id, qa_item_id, qa_error *);
    bool (*use_item)(void *, qa_actor_id, qa_item_id, qa_error *);
    bool (*give_body_armor)(void *, qa_mode_id, qa_actor_id, qa_error *);
    /* Q1 intent replaces the deadline; Q2 intent stacks it and emits the mode
     * source's activation cue. The selected effects owner retains the timer. */
    bool (*give_quad)(void *, qa_mode_id, qa_actor_id, qa_game_family,
                       uint64_t duration_ns, qa_error *);
    bool (*spawn_monster)(void *, qa_mode_id, qa_string_id classname, qa_vec3 origin,
                          qa_vec3 angles, qa_actor_id enemy, qa_actor_id *out, qa_error *);
    bool (*spawn_loot)(void *, qa_mode_id, const qa_mode_loot_spawn *, qa_actor_id *out,
                       qa_error *);
    bool (*grant_loot)(void *, qa_actor_id item, qa_actor_id player, bool *accepted, qa_error *);
    bool (*loot_alpha)(void *, qa_actor_id, float, qa_error *);
    bool (*horde_head)(void *, qa_actor_id, bool enabled, qa_error *);
    /* The source owner returns its next RNG draw in [0,1). An absent hook uses
     * the native mode stream. Horde preserves the source's draw ordering. */
    float (*source_random)(void *, qa_mode_id);
    /* Read live authored state without changing shared deadlines/occupancy.
     * False with QA_OK means the source point no longer exists. */
    bool (*horde_point)(void *, qa_mode_id, qa_actor_id, qa_vec3 *origin, qa_vec3 *angles,
                        qa_string_id *target, uint32_t *flags, qa_error *);
    bool (*horde_manager)(void *, qa_mode_id, qa_actor_id, qa_string_id *target,
                          qa_actor_id *activator, qa_error *);
    bool (*campaign_restart)(void *, qa_mode_id, uint32_t initial_flags, qa_error *);
    bool (*map_allowed)(void *, qa_mode_id, qa_string_id);
    bool (*next_map_allowed)(void *, qa_mode_id);
    bool (*selected_map_command)(void *, qa_mode_id, qa_string_id map,
                                  qa_string_id *command, qa_error *);
    /* Rogue's startup flag belongs to its actual native source world. */
    bool (*rogue_runes_claim)(void *, qa_mode_id, bool *newly_claimed, qa_error *);
    bool (*rogue_runes_read)(void *, qa_mode_id, qa_actor_id *world, bool *started);
    bool (*q3_clock)(void *, qa_mode_id, int32_t *source_time_ms, qa_error *);
    bool (*q3_client_slot)(void *, qa_mode_id, qa_actor_id,
                          qa_actor_owner *, uint32_t *, qa_error *);
    bool (*q3_native_source)(void *, qa_mode_id, qa_actor_owner *);
    /* Actual native GAME score storage is independent of the selected rule.
     * Only its primary mode and fully bound physical client qualify. */
    bool (*q3_source_score_bound)(void *, qa_mode_id, qa_actor_id, qa_actor_owner *);
    bool (*q3_source_match_exit)(void *, qa_mode_id, qa_string_id, qa_error *);
    bool (*q3_team_status_bound)(void *, qa_mode_id);
    bool (*q3_source_object)(void *, qa_mode_id, qa_actor_id, bool *native_source, qa_error *);
    bool (*q3_source_object_view)(void *, qa_mode_id, qa_actor_id, qa_mode_object_view *, qa_error *);
    bool (*q3_rank_client)(void *, qa_mode_id, qa_actor_id, bool *connected,
                          bool *connecting, bool *bot, int32_t *source_team, qa_error *);
    bool (*q3_rank_counts)(void *, qa_mode_id, qa_mode_q3_rank_counts *, qa_error *);
    bool (*q3_choose_team)(void *, qa_mode_id, qa_actor_id, qa_team_id *, qa_error *);
    bool (*q3_vote_calls)(void *, qa_mode_id, qa_actor_id, bool team_vote,
                          int32_t *, qa_error *);
    bool (*q3_team_request)(void *, qa_mode_id, qa_actor_id, qa_team_id,
                           bool spectator, bool automatic, int32_t spectator_state,
                           int32_t spectator_client, bool *accepted, bool *changed, qa_error *);
    bool (*q3_stop_following)(void *, qa_mode_id, qa_actor_id, qa_error *);
    bool (*q3_intermission_client)(void *, qa_mode_id, qa_actor_id, bool *eligible,
                                  bool *ready, qa_error *);
    bool (*q3_intermission_ready_publish)(void *, qa_mode_id, int32_t mask, qa_error *);
    bool (*q3_warmup_restart)(void *, qa_mode_id, qa_error *);
    bool (*team_equipment)(void *, qa_actor_id, qa_item_id *weapon, uint64_t *powerups, qa_error *);
    bool (*select_grapple)(void *, qa_actor_id, qa_error *);
    bool (*drop_arsenal)(void *, qa_actor_id, bool weapon, qa_error *);
    bool (*disconnect)(void *, qa_actor_id, qa_error *);
    bool (*observer_nearby)(void *, qa_actor_id, qa_error *);
    /* Direct source death, bypassing damage admission and armor. Commit shared
     * health then dispatch selected character death and attached cleanup once. */
    bool (*force_death)(void *, const qa_damage_request *, qa_error *);
    /* Source-authored audiovisual events use this mode's actual content owner.
     * The synchronous event and message arguments remain borrowed. */
    bool (*emit)(void *, qa_mode_id, const qa_builtin_event *, qa_error *);
} qa_modes_hooks;
typedef struct qa_modes_options {
    qa_actor_owner owner;
    qa_builtin_services services;
    qa_modes_hooks hooks;
    uint32_t mode_capacity, objective_capacity;
    uint64_t random_seed;
} qa_modes_options;

qa_mode_rules qa_mode_defaults(qa_mode_source, qa_mode_kind);
bool qa_modes_create(const qa_modes_options *, qa_modes **, qa_error *);
/* Single session thread. Read callbacks do not mutate their exposed state.
 * Destruction and removal occur outside synchronous mode/provider calls. */
void qa_modes_destroy(qa_modes *);
bool qa_modes_idle(const qa_modes *);
bool qa_modes_add(qa_modes *, const qa_mode_rules *, qa_mode_id *, qa_error *);
bool qa_modes_remove(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_configure(qa_modes *, qa_mode_id, const qa_mode_rules *, qa_error *);
bool qa_modes_read(qa_modes *, qa_mode_id, qa_mode_view *, qa_error *);
bool qa_modes_q3_settings_read(const qa_modes *, qa_mode_id, qa_mode_q3_settings *,
                               bool *present, qa_error *);
bool qa_modes_q3_settings_admit(qa_modes *, qa_mode_id, const qa_mode_q3_settings *,
                                int32_t source_time_ms, int32_t restarted, qa_error *);
bool qa_modes_q3_settings_update(qa_modes *, qa_mode_id, const qa_mode_q3_settings *, qa_error *);
bool qa_modes_q3_source_phase(qa_modes *, qa_mode_id, qa_mode_phase,
    uint64_t source_time_ns, uint64_t deadline_ns, qa_error *);
bool qa_modes_q3_source_reset_teams(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_q3_source_frame(qa_modes *, qa_mode_id, uint64_t now_ns,
    uint64_t elapsed_ns, qa_error *);
bool qa_modes_at(qa_modes *, size_t index, qa_mode_id *, qa_mode_view *, qa_error *);
bool qa_modes_player(qa_modes *, const qa_match_player *, qa_error *);
bool qa_modes_player_read(qa_modes *, qa_mode_id, qa_actor_id, qa_mode_player_view *, qa_error *);
typedef struct qa_mode_ctf_view {
    int32_t last_team;
    float status, access;
    bool start_map, pregame_over, observer, grapple_disabled;
} qa_mode_ctf_view;
/* Only the admitted ThreeWave instance supplies this source continuation.
 * Grapple mechanic and slot ownership are read separately from equipment. */
bool qa_modes_ctf_read(qa_modes *, qa_mode_id, qa_actor_id, qa_mode_ctf_view *, qa_error *);
bool qa_modes_ctf_restore_player(qa_modes *, qa_mode_id, qa_actor_id,
                                  int32_t last_team, float status, float access, qa_error *);
bool qa_modes_ctf_pregame_end(qa_modes *, qa_mode_id, qa_error *);
/* Physical Q1 source expansion selection. False selected means Rogue
 * delegates to its real base selector; coop/nondeathmatch policy is owned by
 * that source caller. Points and cursors retain actual authored actors. */
bool qa_modes_q1_spawnpoint(qa_modes *, qa_mode_id, qa_actor_id,
    qa_mode_spawnpoint *, bool *selected, qa_error *);
bool qa_modes_join(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id, bool observer, qa_error *);
/* Command admission follows source death/reset/respawn rules. join is the
 * lower-level connection/restore admission and does not synthesize a death. */
bool qa_modes_request_team(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id, bool observer,
                           bool automatic, bool *accepted, qa_error *);
bool qa_modes_suicide(qa_modes *, qa_mode_id, qa_actor_id, bool *handled, qa_error *);
typedef struct qa_mode_controls {
    qa_vec3 view_angles;
    uint64_t teleport_hold_ns;
    int32_t impulse;
    bool jump, prompt_supported, grapple_selected;
} qa_mode_controls;
bool qa_modes_player_controls(qa_modes *, qa_mode_id, qa_actor_id, const qa_mode_controls *,
                              bool *consumed, qa_error *);
/* automatic is 0 (explicit target/free), 1 (first active client) or 2 (second).
 * cycle is -1/0/+1 and traverses the application-owned client order. */
bool qa_modes_follow(qa_modes *, qa_mode_id, qa_actor_id, qa_actor_id target, int automatic,
                     int cycle, bool *accepted, qa_error *);
bool qa_modes_follow_target(qa_modes *, qa_mode_id, qa_actor_id, qa_actor_id *, qa_error *);
bool qa_modes_observe(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id filter, bool *accepted,
                      qa_error *);
bool qa_modes_scoreboard(qa_modes *, qa_mode_id, qa_actor_id, bool visible, qa_error *);
bool qa_modes_choose_team(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id *, qa_error *);
bool qa_modes_statistics(qa_modes *, qa_mode_id, qa_actor_id, qa_mode_statistics *, qa_error *);
bool qa_modes_source_award(qa_modes *, qa_mode_id, qa_actor_id, int32_t source_award, qa_error *);
bool qa_modes_bind_player(qa_modes *, qa_mode_id, qa_actor_id, const qa_match_binding *, qa_match_lease *,
                          qa_error *);
bool qa_modes_unbind_player(qa_modes *, qa_match_lease, qa_error *);
bool qa_modes_score(qa_modes *, qa_mode_id, qa_actor_id, int32_t *, qa_error *);
bool qa_modes_set_score(qa_modes *, qa_mode_id, qa_actor_id, int32_t, qa_error *);
bool qa_modes_add_score(qa_modes *, qa_mode_id, qa_actor_id, int32_t, qa_error *);
bool qa_modes_team(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id *, qa_error *);
bool qa_modes_team_totals(qa_modes *, qa_mode_id, int64_t totals[3], qa_error *);
bool qa_modes_set_team(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id, qa_error *);
bool qa_modes_same_team(qa_modes *, qa_mode_id, qa_actor_id, qa_actor_id);
bool qa_modes_team_score(qa_modes *, qa_mode_id, qa_team_id, int32_t, qa_error *);
bool qa_modes_frame(qa_modes *, qa_mode_id, uint64_t now_ns, uint64_t elapsed_ns, qa_error *);
bool qa_modes_actor_released(qa_modes *, qa_actor_record, qa_error *);
bool qa_modes_rank(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_sorted(qa_modes *, qa_mode_id, qa_actor_id *, size_t capacity, size_t *count,
                     qa_error *);
bool qa_modes_ready(qa_modes *, qa_mode_id, qa_actor_id, bool, qa_error *);
bool qa_modes_start(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_cancel(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_end(qa_modes *, qa_mode_id, qa_string_id reason, qa_error *);
typedef struct qa_mode_frag {
    qa_mode_id mode;
    qa_actor_id recipient;
    int32_t delta;
    /* Set when a source obituary has already called a mode scoring policy. */
    qa_mode_id evaluated_mode;
} qa_mode_frag;
bool qa_modes_player_death(qa_modes *, qa_mode_id, const qa_damage_outcome *, qa_error *);
bool qa_modes_q3_source_death_score(qa_modes *, qa_mode_id, qa_actor_id recipient,
    int32_t amount, qa_error *);
/* apply_ordinary_score is local to this mode: false means its score owner
 * already applied the ordinary obituary delta, never that another mode scored. */
bool qa_modes_player_death_component(qa_modes *, qa_mode_id, const qa_damage_outcome *,
                                     bool apply_ordinary_score, const qa_mode_frag *ordinary, qa_error *);
bool qa_modes_player_hurt(qa_modes *, qa_mode_id, const qa_damage_request *, qa_error *);
bool qa_modes_player_respawn(qa_modes *, qa_mode_id, qa_actor_id, qa_error *);
/* Actual authored manager retained by one configured Horde instance. */
bool qa_modes_horde_manager_actor(qa_modes *, qa_mode_id, qa_actor_id *, qa_error *);
bool qa_modes_rogue_tag_score(qa_modes *, qa_mode_id, qa_actor_id victim, qa_actor_id attacker,
                              int32_t *points, qa_error *);
typedef enum qa_mode_admin_action {
    QA_MODE_ADMIN_PAUSE,
    QA_MODE_ADMIN_PAUSE_ANY,
    QA_MODE_ADMIN_LOCK,
    QA_MODE_ADMIN_START,
    QA_MODE_ADMIN_STOP,
    QA_MODE_ADMIN_MAP,
    QA_MODE_ADMIN_MATCH_MAP
} qa_mode_admin_action;
/* Authentication and command parsing precede these typed operations. Passwords
 * never enter saved mode state. Referee: 0 none, 1 referee, 2 remote admin. */
bool qa_modes_referee(qa_modes *, qa_mode_id, qa_actor_id, unsigned level, qa_error *);
bool qa_modes_admin(qa_modes *, qa_mode_id, qa_actor_id, qa_mode_admin_action, qa_string_id map,
                    bool *accepted, qa_error *);
bool qa_modes_can_move(qa_modes *, qa_mode_id, qa_actor_id);
bool qa_modes_console_command(qa_modes *, qa_mode_id, qa_actor_id,
                               const qa_command_invocation *, bool *handled, qa_error *);
typedef struct qa_mode_location {
    qa_actor_id actor;
    qa_string_id message;
    int32_t id, color;
} qa_mode_location;
typedef struct qa_mode_team_row {
    qa_actor_id actor;
    int32_t location;
    float health, armor;
    qa_item_id weapon;
    uint64_t powerups;
} qa_mode_team_row;
bool qa_modes_location(qa_modes *, qa_mode_id, qa_actor_id, qa_mode_location *, qa_error *);
bool qa_modes_team_info(qa_modes *, qa_mode_id, qa_actor_id recipient, qa_mode_team_row *,
                        size_t capacity, size_t *count, qa_error *);

bool qa_modes_bind_objective(qa_modes *, const qa_objective_binding *, qa_objective_lease *,
                             qa_error *);
bool qa_modes_unbind_objective(qa_modes *, qa_objective_lease, qa_error *);
bool qa_modes_objective(qa_modes *, qa_mode_id, qa_string_id, qa_objective_state *, qa_error *);
bool qa_modes_change_objective(qa_modes *, qa_mode_id, qa_string_id, const qa_objective_state *, qa_error *);
bool qa_modes_campaign_gates(qa_modes *, bool *complete, qa_error *);
bool qa_modes_objective_at(qa_modes *, size_t index, qa_objective_binding *, qa_objective_state *,
                           qa_error *);
bool qa_modes_spawn_object(qa_modes *, qa_mode_id, const qa_mode_object_spec *, qa_actor_id *,
                           qa_error *);
bool qa_modes_touch(qa_modes *, qa_actor_id object, qa_actor_id player, bool *accepted, qa_error *);
bool qa_modes_drop(qa_modes *, qa_mode_id, qa_actor_id player, bool death, qa_error *);
/* Publish after map objectives and native catalogs, and again after mode
 * selection changes. Existing native descriptors retain their owner. The
 * command coordinator consults item_action before ordinary inventory action. */
bool qa_modes_publish_items(qa_modes *, qa_actor_id, qa_error *);
typedef enum qa_mode_console_give {
    QA_MODE_GIVE_PICKUP, QA_MODE_GIVE_INVENTORY_ONLY,
    QA_MODE_GIVE_INDIVIDUAL_ONLY, QA_MODE_GIVE_FORBIDDEN
} qa_mode_console_give;
typedef struct qa_mode_item {
    qa_item_definition definition;
    qa_mode_id mode;
    qa_item_id source_item;
    const char *classname;
    double capacity;
    qa_mode_console_give console_give;
} qa_mode_item;
/* B34 adapts one selected mode's catalog to native command item lookup.
 * Definitions exist before map objects spawn; labels/classnames are static. */
size_t qa_modes_item_count(qa_modes *, qa_mode_id);
bool qa_modes_item_at(qa_modes *, qa_mode_id, size_t, qa_mode_item *, qa_error *);
/* direct is startitems' source grant; false includes shared pickup policy and
 * ordinary pickup feedback. count=0 selects the source default. */
bool qa_modes_give_item(qa_modes *, qa_mode_id, qa_actor_id, qa_item_id,
                        bool direct, int32_t count, bool *accepted, qa_error *);
/* Item operations accept the scoped definition.item returned by item_at or
 * the shared inventory catalog, never an unscoped source item identity. */
bool qa_modes_item_action(qa_modes *, qa_mode_id, qa_actor_id, qa_item_id, qa_item_action, bool *handled,
                          qa_error *);
bool qa_modes_object_read(qa_modes *, qa_actor_id, qa_mode_object_view *);
bool qa_modes_object_home(qa_modes *, qa_actor_id, qa_vec3 *, qa_bounds *, qa_error *);
bool qa_modes_object_at(qa_modes *, size_t index, qa_actor_id *, qa_mode_object_view *, qa_error *);
bool qa_modes_object_reaction(qa_modes *, const qa_damage_outcome *, qa_error *);
bool qa_modes_object_damage(qa_modes *, qa_mode_id, qa_damage_request *, bool *allowed, qa_error *);
bool qa_modes_physics(qa_modes *, qa_actor_id, qa_physics_properties *);
bool qa_modes_physics_write(qa_modes *, qa_actor_id, const qa_physics_properties *, qa_error *);
bool qa_modes_spawnpoints(qa_modes *, qa_mode_id, const qa_mode_spawnpoint *, size_t, qa_error *);
bool qa_modes_spawnpoint(qa_modes *, qa_mode_id, qa_actor_id, bool farthest, qa_mode_spawnpoint *,
                         qa_error *);

/* Source damage stages are intentionally distinct: outgoing multipliers,
 * resistance after powered armor, and vampire after actual health damage. */
bool qa_modes_attack_damage(qa_modes *, qa_mode_id, qa_actor_id, float, float *, qa_error *);
bool qa_modes_resist_damage(qa_modes *, qa_mode_id, qa_actor_id, float, float *, qa_error *);
bool qa_modes_after_damage(qa_modes *, qa_mode_id, const qa_damage_outcome *, qa_error *);
bool qa_modes_damage_effect(qa_modes *, qa_mode_id, qa_damage_effect_stage,
                            const qa_damage_request *, qa_damage_effect *, qa_error *);
bool qa_modes_haste_weapon(qa_modes *, qa_mode_id, qa_actor_id, qa_item_id weapon,
                           float base_interval, float *interval, float *nail_speed, qa_error *);
/* Actual firing cooldown producer; observations use haste_weapon above. */
bool qa_modes_weapon_attack_delay(qa_modes *, qa_mode_id, qa_actor_id, qa_item_id weapon,
                                  float *delay, qa_error *);
qa_team_id qa_modes_combat_team(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id fallback);
bool qa_modes_has_relic(qa_modes *, qa_mode_id, qa_actor_id, qa_relic_kind);
bool qa_modes_tech_sound(qa_modes *, qa_mode_id, qa_actor_id, qa_relic_kind, bool quad,
                         bool silenced, bool *handled, qa_error *);
bool qa_modes_grapple_allowed(qa_modes *, qa_mode_id, qa_actor_id owner, qa_actor_id target,
                              bool pulse);
bool qa_modes_grapple_hit(qa_modes *, qa_mode_id, qa_actor_id, qa_error *);

typedef struct qa_mode_vote {
    qa_match_intent intent;
    qa_actor_id initiator;
    qa_team_id team;
    uint64_t deadline_ns, execute_ns;
    int32_t yes, no, needed;
    bool active, passed;
} qa_mode_vote;
bool qa_modes_vote_start(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id, const qa_match_intent *,
                         qa_error *);
bool qa_modes_vote_cast(qa_modes *, qa_mode_id, qa_actor_id, qa_team_id, bool yes, qa_error *);
bool qa_modes_vote_read(qa_modes *, qa_mode_id, qa_team_id, qa_mode_vote *, qa_error *);
bool qa_modes_ghost_rejoin(qa_modes *, qa_mode_id, qa_actor_id, uint32_t code, qa_error *);

#endif
