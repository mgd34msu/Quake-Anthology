#ifndef QA_ARENA_PROGRESS_H
#define QA_ARENA_PROGRESS_H

#include "qa/console.h"

typedef struct qa_arena_catalog {
    int32_t regular_levels, total_levels;
    int32_t training, final; /* -1 when absent. */
} qa_arena_catalog;
typedef struct qa_arena_progress {
    qa_cvars *cvars;
    qa_arena_catalog catalog;
} qa_arena_progress;
typedef struct qa_arena_best {
    int32_t rank, skill;
} qa_arena_best;
typedef struct qa_arena_result {
    int32_t level, skill, rank, accuracy, impressive, excellent, gauntlet, frags;
    bool perfect;
} qa_arena_result;
typedef struct qa_arena_award {
    int32_t medal, amount;
} qa_arena_award;
typedef struct qa_arena_postgame {
    int32_t rank, completed_tier, unlocked_movie, next_level;
    qa_arena_award awards[6];
    size_t award_count;
} qa_arena_postgame;

/* Borrows the selected source's cvar registry; its existing archive owner also
 * persists progression. There is no second scores/awards/video store. Initialize
 * at source setup. Record/reset/unlock use source cvar notification order; the
 * application owns the archive transaction and round-identity deduplication. */
bool qa_arena_progress_init(qa_arena_progress *, qa_cvars *, qa_arena_catalog, uint64_t owner,
                            qa_error *);
bool qa_arena_progress_best(const qa_arena_progress *, int32_t level, qa_arena_best *, qa_error *);
bool qa_arena_progress_award(const qa_arena_progress *, int32_t medal, int32_t *, qa_error *);
bool qa_arena_progress_movie(const qa_arena_progress *, int32_t tier, bool *, qa_error *);
bool qa_arena_progress_current(const qa_arena_progress *, int32_t *, qa_error *);
bool qa_arena_progress_available(const qa_arena_progress *, int32_t level, bool *, qa_error *);
bool qa_arena_progress_tier(const qa_arena_progress *, int32_t level, int32_t *, qa_error *);
bool qa_arena_progress_record(qa_arena_progress *, const qa_arena_result *, qa_arena_postgame *,
                              qa_error *);
bool qa_arena_progress_reset(qa_arena_progress *, qa_error *);
bool qa_arena_progress_unlock_levels(qa_arena_progress *, qa_error *);
bool qa_arena_progress_unlock_medals(qa_arena_progress *, qa_error *);

#endif
