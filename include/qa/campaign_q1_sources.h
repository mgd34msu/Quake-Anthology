#ifndef QA_CAMPAIGN_Q1_SOURCES_H
#define QA_CAMPAIGN_Q1_SOURCES_H
#include "qa/campaign_q1.h"
#include "qa/game_q1.h"

enum qa_q1_campaign_flags {
    QA_Q1_MG1_ALL_SIGILS = 31u,
    QA_Q1_MG1_LAST_SIGIL_SHIFT = 6u,
    QA_Q1_MG3_RUNES = 15u,
    QA_Q1_BLOODY_NIGHTMARE_ACTIVE = 64u,
    QA_Q1_BLOODY_NIGHTMARE_DISCOVERED = 128u,
    QA_Q1_BLOODY_NIGHTMARE_NEWGAME = 256u
};
typedef enum qa_q1_campaign_text {
    QA_Q1_CAMPAIGN_ENDTEXT,
    QA_Q1_CAMPAIGN_INTERMISSION_TEXT
} qa_q1_campaign_text;
typedef enum qa_q1_campaign_timer {
    QA_Q1_CAMPAIGN_CHECK_FINALE,
    QA_Q1_CAMPAIGN_FINISH_FINALE
} qa_q1_campaign_timer;
typedef struct qa_q1_campaign_source_options {
    qa_session *session;
    qa_q1_program program;
    qa_string_id current_map;
    qa_actor_id world;
    uint32_t *server_flags;
    bool rerelease, coop, deathmatch, registered, official_campaign;
    int32_t skill;
    void *context;
    qa_string_id (*read_text)(void *, qa_actor_id, qa_q1_campaign_text);
    bool (*write_text)(void *, qa_actor_id, qa_q1_campaign_text, qa_string_id, qa_error *);
    float (*cvar)(void *, const char *);
    bool (*command)(void *, const char *, qa_error *);
    bool (*achievement)(void *, const char *, qa_error *);
    /* B14 cancels the actual manager and schedules source monster cleanup. */
    bool (*finish_horde)(void *, double seconds, qa_error *);
    bool (*finale_finished)(void *);
    bool (*schedule)(void *, qa_q1_campaign_timer, double delay_seconds, qa_error *);
    /* Direct transition owner, bypassing these already-evaluated source rules. */
    bool (*travel)(void *, qa_string_id, qa_actor_id cause, qa_error *);
} qa_q1_campaign_source_options;
typedef struct qa_q1_campaign_source qa_q1_campaign_source;
qa_q1_campaign_source *qa_q1_campaign_source_create(const qa_q1_campaign_source_options *,
                                                    qa_error *);
void qa_q1_campaign_source_destroy(qa_q1_campaign_source *);
/* The source outlives the level that copies this borrowed-context rule. */
bool qa_q1_campaign_source_rule(qa_q1_campaign_source *, qa_q1_intermission_rule *, qa_error *);
bool qa_q1_campaign_source_timer(qa_q1_campaign_source *, qa_q1_campaign_timer, qa_error *);
bool qa_q1_campaign_source_rogue_end(qa_q1_campaign_source *, qa_error *);
uint32_t qa_q1_mg1_last_sigil(uint32_t flags);
uint32_t qa_q1_mg1_clear_last_sigil(uint32_t flags);
unsigned qa_q1_mg3_rune_count(uint32_t flags);
#endif
