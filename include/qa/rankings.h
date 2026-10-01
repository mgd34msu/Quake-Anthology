#ifndef QA_RANKINGS_H
#define QA_RANKINGS_H

#include "qa/common.h"

typedef struct qa_rankings qa_rankings;
/* Result classification of the last completed lifecycle operation. Only a
 * failing installed provider callback sets this; local validation/allocation
 * failures and an operation still in progress never qualify. */
bool qa_rankings_backend_failed(const qa_rankings *);
typedef struct qa_ranking_account {
    uint64_t player_id;
    double rank;
} qa_ranking_account;
typedef enum qa_ranking_request_kind {
    QA_RANKING_LOGIN,
    QA_RANKING_CREATE_ACCOUNT
} qa_ranking_request_kind;
typedef struct qa_ranking_request {
    qa_ranking_request_kind kind;
    const char *username, *password, *email;
} qa_ranking_request;
typedef struct qa_ranking_login {
    bool accepted;
    qa_ranking_account account;
    char reason[256];
} qa_ranking_login;
typedef enum qa_ranking_report_kind {
    QA_RANKING_INTEGER,
    QA_RANKING_STRING
} qa_ranking_report_kind;
typedef struct qa_ranking_stat {
    qa_ranking_report_kind kind;
    int32_t key;
    union {
        struct {
            /* Source reports retain the Number value. Match cvar reports can
             * exceed signed 32 bits; their actual producer performs trunc. */
            double value;
            bool accumulate;
        } integer;
        const char *string;
    } value;
} qa_ranking_stat;
typedef struct qa_ranking_report {
    uint64_t self, other;
    qa_ranking_stat stat;
} qa_ranking_report;
/* Native gameplay emits source slots to an application-owned report queue.
 * Slot -1 means the match. Account IDs are resolved by the lifecycle at flush;
 * the consumer copies string values before this borrowed event expires. */
typedef struct qa_ranking_source_report {
    int32_t self, other;
    qa_ranking_stat stat;
} qa_ranking_source_report;
typedef struct qa_ranking_provider {
    void *context;
    const char *endpoint;
    bool (*begin)(void *, const char *game_key, uint64_t *match, qa_error *);
    bool (*login)(void *, uint64_t match, const qa_ranking_request *, qa_ranking_login *,
                  qa_error *);
    bool (*join)(void *, uint64_t match, qa_ranking_account, qa_error *);
    bool (*report)(void *, uint64_t match, const qa_ranking_report *, qa_error *);
    bool (*poll)(void *, qa_error *);
    bool (*logout)(void *, uint64_t match, qa_ranking_account, qa_error *);
    bool (*finish)(void *, uint64_t match, qa_error *);
} qa_ranking_provider;
typedef enum qa_ranking_service_kind {
    QA_RANKING_DISABLED,
    QA_RANKING_UNAVAILABLE,
    QA_RANKING_STARTING,
    QA_RANKING_ACTIVE,
    QA_RANKING_ENDING
} qa_ranking_service_kind;
typedef struct qa_ranking_state {
    qa_ranking_service_kind kind;
    uint64_t game_id;
    char reason[256];
} qa_ranking_state;
typedef enum qa_ranking_player_kind {
    QA_RANKING_NEW_PLAYER,
    QA_RANKING_SPECTATOR,
    QA_RANKING_PENDING_PLAYER,
    QA_RANKING_ACTIVE_PLAYER,
    QA_RANKING_DENIED_PLAYER
} qa_ranking_player_kind;
typedef struct qa_ranking_player {
    qa_ranking_player_kind kind;
    qa_ranking_account account;
    char reason[256];
} qa_ranking_player;
typedef struct qa_ranking_observers {
    void *context;
    void (*player)(void *, int32_t slot, const qa_ranking_player *);
    void (*service)(void *, const qa_ranking_state *);
} qa_ranking_observers;

/* The application serializes these operations on its service queue. Provider
 * callbacks complete synchronously; callbacks may inspect but must not mutate
 * or destroy this lifecycle. Provider context outlives it. Credentials are
 * borrowed only for login and are never retained or included in diagnostics.
 * A NULL provider is the approved unavailable-backend extension point. */
bool qa_rankings_create(const qa_ranking_provider *, const qa_ranking_observers *, qa_rankings **,
                        qa_error *);
qa_ranking_state qa_rankings_state(const qa_rankings *);
qa_ranking_player qa_rankings_player(const qa_rankings *, int32_t slot);
bool qa_rankings_begin(qa_rankings *, bool enabled, bool single_player, const char *game_key,
                       qa_error *);
bool qa_rankings_account(qa_rankings *, int32_t slot, const qa_ranking_request *, qa_error *);
/* -1 denotes the match itself (provider ID zero); inactive players are skipped. */
bool qa_rankings_report_integer(qa_rankings *, int32_t self, int32_t other, int32_t key,
                                double value, bool accumulate, qa_error *);
bool qa_rankings_report_string(qa_rankings *, int32_t self, int32_t other, int32_t key,
                               const char *value, qa_error *);
bool qa_rankings_poll(qa_rankings *, qa_error *);
bool qa_rankings_reset(qa_rankings *, int32_t slot, qa_error *);
bool qa_rankings_spectate(qa_rankings *, int32_t slot, qa_error *);
bool qa_rankings_disconnect(qa_rankings *, int32_t slot, qa_error *);
bool qa_rankings_unavailable(qa_rankings *, const char *reason, qa_error *);
/* End attempts every active logout and finish even when earlier cleanup fails. */
bool qa_rankings_end(qa_rankings *, qa_error *);
bool qa_rankings_close(qa_rankings *, qa_error *);
bool qa_rankings_close_ready(const qa_rankings *);

#endif
