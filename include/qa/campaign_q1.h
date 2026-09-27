#ifndef QA_CAMPAIGN_Q1_H
#define QA_CAMPAIGN_Q1_H
#include "qa/builtin.h"

typedef struct qa_q1_level qa_q1_level;
typedef enum qa_q1_intermission_kind {
    QA_Q1_INTERMISSION_WAITING,
    QA_Q1_INTERMISSION_TRAVEL,
    QA_Q1_INTERMISSION_FINALE,
    QA_Q1_INTERMISSION_SELL
} qa_q1_intermission_kind;
typedef struct qa_q1_intermission_result {
    qa_q1_intermission_kind kind;
    qa_string_id map, text;
    int32_t track;
} qa_q1_intermission_result;
typedef enum qa_q1_finale_decision {
    QA_Q1_FINALE_DELEGATE,
    QA_Q1_FINALE_PRESENT,
    QA_Q1_FINALE_TRAVEL
} qa_q1_finale_decision;
/* Source rules are copied in registration order. Contexts remain borrowed;
 * callbacks may update the level but must not destroy it during invocation. */
typedef struct qa_q1_intermission_rule {
    qa_string_id id;
    void *context;
    bool (*touch)(void *, qa_q1_level *, qa_actor_id trigger, qa_actor_id player, qa_error *);
    bool (*begin)(void *, qa_q1_level *, qa_string_id map, qa_actor_id cause, double seconds,
                  qa_error *);
    bool (*finale)(void *, qa_q1_level *, uint32_t stage, qa_string_id next_map, double seconds,
                   qa_q1_finale_decision *, qa_q1_intermission_result *, qa_error *);
    bool (*travel)(void *, qa_q1_level *, qa_string_id map, qa_actor_id cause, bool *handled,
                   qa_error *);
} qa_q1_intermission_rule;
typedef struct qa_q1_level_state {
    qa_string_id next_map;
    qa_actor_id cause;
    uint32_t stage;
    double exit_after;
    bool intermission;
} qa_q1_level_state;
typedef struct qa_q1_level_player {
    qa_actor_id actor;
    bool fired_weapon, took_damage;
} qa_q1_level_player;
typedef struct qa_q1_level_options {
    qa_builtin_services services;
    qa_string_id current_map;
    /* Persistent campaign authority, shared by level and selected source rules. */
    uint32_t *server_flags;
    bool rerelease, deathmatch, registered, official_campaign;
    int32_t skill;
    const qa_q1_intermission_rule *rules;
    size_t rule_count;
    void *context;
    bool (*begin)(void *, qa_string_id map, qa_actor_id cause, double exit_after, qa_error *);
    bool (*travel)(void *, qa_string_id map, qa_actor_id cause, qa_error *);
    bool (*achievement)(void *, qa_actor_id player, const char *id, qa_error *);
    /* Allocate a real source nextlevel actor. Its think calls begin_pending. */
    bool (*defer_begin)(void *, double delay_seconds, qa_error *);
} qa_q1_level_options;
qa_q1_level *qa_q1_level_create(const qa_q1_level_options *, qa_error *);
void qa_q1_level_destroy(qa_q1_level *);
const qa_q1_level_state *qa_q1_level_read(const qa_q1_level *);
bool qa_q1_level_reset_player(qa_q1_level *, qa_actor_id, qa_error *);
void qa_q1_level_actor_released(qa_q1_level *, qa_actor_id);
bool qa_q1_level_note_attack(qa_q1_level *, qa_actor_id, bool axe_only, qa_error *);
bool qa_q1_level_note_damage(qa_q1_level *, qa_actor_id, float health_damage, qa_error *);
bool qa_q1_level_touch(qa_q1_level *, qa_actor_id trigger, qa_actor_id player, qa_error *);
bool qa_q1_level_travel(qa_q1_level *, qa_string_id map, qa_actor_id cause, qa_error *);
bool qa_q1_level_begin(qa_q1_level *, qa_string_id map, qa_actor_id cause, double seconds,
                       qa_error *);
bool qa_q1_level_begin_pending(qa_q1_level *, double seconds, qa_error *);
bool qa_q1_level_cutscene(qa_q1_level *, qa_string_id map, qa_actor_id cause, double exit_after,
                          qa_error *);
bool qa_q1_level_defer_exit(qa_q1_level *, double until_seconds, qa_error *);
bool qa_q1_level_check_limits(qa_q1_level *, double seconds, const float *scores, size_t count,
                              float timelimit_minutes, float fraglimit,
                              qa_string_id first_changelevel_map, bool *triggered, qa_error *);
bool qa_q1_level_request_exit(qa_q1_level *, double seconds, bool pressed, bool same_level,
                              qa_q1_intermission_result *, qa_error *);
bool qa_q1_level_client_connected(qa_q1_level *, double seconds, bool same_level,
                                  qa_q1_intermission_result *, qa_error *);
/* Classic text is static; rerelease returns the localization key unchanged. */
const char *qa_q1_finale_text(bool rerelease, const char *key);
typedef struct qa_q1_level_checkpoint {
    qa_q1_level_state state;
    uint32_t server_flags;
    qa_q1_level_player *players;
    size_t player_count;
} qa_q1_level_checkpoint;
bool qa_q1_level_capture(const qa_q1_level *, qa_q1_level_checkpoint *, qa_error *);
void qa_q1_level_checkpoint_free(qa_q1_level_checkpoint *);
/* Restore after B30 explicitly remaps actor/string IDs into the new session. */
bool qa_q1_level_restore(qa_q1_level *, const qa_q1_level_checkpoint *, qa_error *);

typedef enum qa_q1_spawn_kind {
    QA_Q1_SPAWN_START,
    QA_Q1_SPAWN_RETURN,
    QA_Q1_SPAWN_COOP,
    QA_Q1_SPAWN_DEATHMATCH,
    QA_Q1_SPAWN_TEST
} qa_q1_spawn_kind;
typedef struct qa_q1_spawn_point {
    qa_actor_id actor;
    qa_q1_spawn_kind kind;
} qa_q1_spawn_point;
typedef enum qa_q1_spawn_decision {
    QA_Q1_SPAWN_DELEGATE,
    QA_Q1_SPAWN_DEFERRED,
    QA_Q1_SPAWN_SELECTED
} qa_q1_spawn_decision;
typedef struct qa_q1_spawn_rule {
    qa_string_id id;
    void *context;
    bool (*select)(void *, bool force, qa_q1_spawn_decision *, qa_actor_id *, qa_error *);
} qa_q1_spawn_rule;
typedef struct qa_q1_spawn_options {
    qa_builtin_services services;
    uint32_t *server_flags;
    bool rerelease, coop, deathmatch;
    void *context;
    double (*random)(void *);
    const qa_q1_spawn_rule *rules;
    size_t rule_count;
} qa_q1_spawn_options;
typedef struct qa_q1_spawn_selector qa_q1_spawn_selector;
qa_q1_spawn_selector *qa_q1_spawn_selector_create(const qa_q1_spawn_options *, qa_error *);
void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *);
/* Points retain authored source order. Read-only callbacks must not mutate the
 * point array or recursively invoke this selector. A null result is a source
 * deferral; the admission owner retries and forces after five seconds. */
bool qa_q1_spawn_select(qa_q1_spawn_selector *, const qa_q1_spawn_point *, size_t count, bool force,
                        qa_actor_id *out, qa_error *);
qa_actor_id qa_q1_spawn_last(const qa_q1_spawn_selector *);
bool qa_q1_spawn_restore_last(qa_q1_spawn_selector *, qa_actor_id, qa_error *);
#endif
