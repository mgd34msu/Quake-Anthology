/* Port of src/app/bootstrap/base-arena-progression.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_PROGRESSION_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_PROGRESSION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/core/cvars/index.h"
/* TS ArenaSkill is number 1..5; training/final/unlocked_movie -1 = TS null. */
typedef struct qa_app_arena_progression_catalog {
  double regular_levels, total_levels, training, final;
} qa_app_arena_progression_catalog;
typedef struct qa_app_arena_result {
  double level, skill, rank, accuracy, impressive, excellent, gauntlet, frags;
  bool perfect;
} qa_app_arena_result;
typedef struct qa_app_arena_award { double medal, amount; } qa_app_arena_award;
typedef struct qa_app_arena_progression_result {
  double rank, completed_tier, unlocked_movie, next_level;
  qa_app_arena_award *awards; size_t award_count; /* owned; free awards */
} qa_app_arena_progression_result;
typedef struct qa_app_arena_best { double rank, skill; } qa_app_arena_best;
typedef struct qa_app_base_arena_progression qa_app_base_arena_progression;
qa_app_base_arena_progression *qa_app_base_arena_progression_create(qa_cvar_registry *cvars, qa_app_arena_progression_catalog catalog);
void qa_app_base_arena_progression_destroy(qa_app_base_arena_progression *progression);
qa_app_arena_best qa_app_base_arena_progression_best(qa_app_base_arena_progression *progression, double level);
double qa_app_base_arena_progression_award(qa_app_base_arena_progression *progression, double medal);
bool qa_app_base_arena_progression_movie_unlocked(qa_app_base_arena_progression *progression, double tier);
double qa_app_base_arena_progression_current_level(qa_app_base_arena_progression *progression);
bool qa_app_base_arena_progression_level_available(qa_app_base_arena_progression *progression, double level);
double qa_app_base_arena_progression_completed_tier(qa_app_base_arena_progression *progression, double level);
qa_app_arena_progression_result qa_app_base_arena_progression_record(qa_app_base_arena_progression *progression, qa_app_arena_result result);
void qa_app_base_arena_progression_reset(qa_app_base_arena_progression *progression);
void qa_app_base_arena_progression_unlock_levels(qa_app_base_arena_progression *progression);
void qa_app_base_arena_progression_unlock_medals(qa_app_base_arena_progression *progression);
#endif
