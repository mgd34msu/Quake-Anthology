#ifndef QA_LAUNCH_IDENTITY_FIELDS_H
#define QA_LAUNCH_IDENTITY_FIELDS_H

#define QA_LAUNCH_MODE_RULE_FIELDS(FIELD) \
    FIELD(U, source) \
    FIELD(U, kind) \
    FIELD(U, teams[0]) \
    FIELD(U, teams[1]) \
    FIELD(U, teams[2]) \
    FIELD(U, forced_team) \
    FIELD(I, frag_limit) \
    FIELD(I, capture_limit) \
    FIELD(I, warmup_seconds) \
    FIELD(I, competition) \
    FIELD(I, setup_seconds) \
    FIELD(I, countdown_seconds) \
    FIELD(I, match_seconds) \
    FIELD(I, max_game_players) \
    FIELD(I, election_percent) \
    FIELD(I, teamplay) \
    FIELD(I, rune_mask) \
    FIELD(I, vote_limit) \
    FIELD(U, flags) \
    FIELD(U, referee_flags) \
    FIELD(F, time_limit_minutes) \
    FIELD(F, obelisk_health) \
    FIELD(F, obelisk_regen) \
    FIELD(W, obelisk_regen_ns) \
    FIELD(W, obelisk_respawn_ns) \
    FIELD(B, enabled) \
    FIELD(B, friendly_fire) \
    FIELD(B, force_join) \
    FIELD(B, match_lock) \
    FIELD(B, paused) \
    FIELD(B, auto_lock) \
    FIELD(B, relics) \
    FIELD(B, single_player_active) \
    FIELD(B, tournament_restart) \
    FIELD(B, q2_rerelease) \
    FIELD(B, start_map) \
    FIELD(B, force_balance) \
    FIELD(B, voting_disabled) \
    FIELD(B, rogue_deathmatch)

#define QA_LAUNCH_MODE_RULE_COUNT(kind, member) + 1
enum { QA_LAUNCH_MODE_RULE_FIELD_COUNT = 0 QA_LAUNCH_MODE_RULE_FIELDS(QA_LAUNCH_MODE_RULE_COUNT) };
#undef QA_LAUNCH_MODE_RULE_COUNT

#endif
