/* Port of src/content/q1/base/rules.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_RULES_H
#define QA_CONTENT_Q1_BASE_RULES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/foundation_types.h"
#include "qa/persistence/value.h"

/* TS Q1CampaignBinding is owned by provider.h (provider.ts). */
#include "qa/content/q1/base/provider.h"

typedef struct qa_q1_spawn_selector qa_q1_spawn_selector;
/* Selection returns: 0 = TS undefined (delegate), 1 = point, 2 = TS null. */
typedef int (*qa_q1_spawn_selection_fn)(void *ctx, bool force_spawn, qa_q1b_actor **out);
qa_q1_spawn_selector *qa_q1_spawn_selector_create(qa_q1b_game *game, qa_q1_campaign_binding campaign);
void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *selector);
void qa_q1_spawn_selector_register_selection(qa_q1_spawn_selector *selector, const char *id,
                                             qa_q1_spawn_selection_fn select, void *ctx);
qa_save_value *qa_q1_spawn_selector_capture(const qa_q1_spawn_selector *selector);
void qa_q1_spawn_selector_restore(qa_q1_spawn_selector *selector, const qa_save_reader *reader);
qa_q1b_actor *qa_q1_spawn_selector_select(qa_q1_spawn_selector *selector, bool force_spawn);

/* TS Q1ObituaryActor / Q1Obituary / Q1ClientNotice. */
typedef struct qa_q1_obituary_actor {
  qa_actor_id actor;
  const char *name; const char *classname;
  bool is_player; bool is_monster;
  double team; double health;
  const char *water_type; /* "empty"|"water"|"slime"|"lava" */
  double water_level;
  const char *weapon; /* NULL = TS null */
  double quad_expires; double invulnerable_expires;
  bool brush;
  const char *kill_string;
} qa_q1_obituary_actor;
typedef struct qa_q1_obituary {
  bool has_message; char text[512];
  const char *arguments[2]; size_t argument_count;
  bool has_score; qa_actor_id score_actor; double score_delta;
  bool has_achievement; qa_actor_id achievement_actor; const char *achievement_id;
} qa_q1_obituary;
typedef struct qa_q1_obituary_input {
  const char *edition;
  qa_q1_obituary_actor victim;
  const qa_q1_obituary_actor *attacker;
  const qa_q1_obituary_actor *telefrag_owner;
  double teamplay;
  const char *death_type;
  void *random_ctx; double (*random)(void *ctx);
} qa_q1_obituary_input;
qa_q1_obituary qa_q1_obituary_compute(const qa_q1_obituary_input *input);
typedef struct qa_q1_client_notice {
  char text[512]; const char *key; /* rerelease key when text is a $qc id */
  const char *arguments[2]; char frags[32]; size_t argument_count;
  double score_delta;
} qa_q1_client_notice;
/* event is "connect"|"disconnect"|"suicide"|"exit". */
qa_q1_client_notice qa_q1_client_notice_make(const char *edition, const char *event,
                                             const char *name, double frags);

/* TS Q1SourceFinale / Q1IntermissionResult (kind strings) and rule. */
typedef struct qa_q1_intermission_result {
  const char *kind; /* "waiting"|"travel"|"finale"|"sell-screen" */
  char map[64]; char text[4096]; double track;
} qa_q1_intermission_result;
/* finale returns: 0 = TS undefined, 1 = finale result, 2 = TS null. */
typedef struct qa_q1_intermission_rule {
  const char *id; void *ctx;
  void (*touch)(void *ctx, qa_q1b_actor *trigger, qa_actor_id player);
  void (*begin)(void *ctx, const char *map, qa_q1b_opt_actor cause);
  int (*finale)(void *ctx, double stage, const char *next_map, qa_q1_intermission_result *out);
  bool (*travel)(void *ctx, const char *map, qa_q1b_opt_actor cause);
} qa_q1_intermission_rule;

typedef struct qa_q1_level_rules qa_q1_level_rules;
qa_q1_level_rules *qa_q1_level_rules_create(qa_q1b_game *game, qa_q1_campaign_binding campaign,
                                            bool registered, bool official_campaign);
void qa_q1_level_rules_destroy(qa_q1_level_rules *rules);
qa_save_value *qa_q1_level_rules_capture(const qa_q1_level_rules *rules);
void qa_q1_level_rules_restore(qa_q1_level_rules *rules, const qa_save_reader *reader);
void qa_q1_level_rules_reset_player(qa_q1_level_rules *rules, qa_owned_actor actor);
void qa_q1_level_rules_register_rule(qa_q1_level_rules *rules, qa_q1_intermission_rule rule);
void qa_q1_level_rules_changelevel_touched(qa_q1_level_rules *rules, qa_q1b_actor *trigger, qa_actor_id player);
void qa_q1_level_rules_travel_to(qa_q1_level_rules *rules, const char *map, qa_q1b_opt_actor cause);
void qa_q1_level_rules_note_attack(qa_q1_level_rules *rules, qa_owned_actor actor, bool axe_only);
void qa_q1_level_rules_note_damage(qa_q1_level_rules *rules, qa_owned_actor actor, double health_damage);
void qa_q1_level_rules_begin(qa_q1_level_rules *rules, const char *map, qa_q1b_opt_actor cause);
void qa_q1_level_rules_begin_cutscene(qa_q1_level_rules *rules, const char *map,
                                      qa_q1b_opt_actor cause, double exit_after);
bool qa_q1_level_rules_check_limits(qa_q1_level_rules *rules, double seconds,
                                    const double *scores, size_t score_count,
                                    double timelimit_minutes, double fraglimit);
qa_q1_intermission_result qa_q1_level_rules_request_exit(qa_q1_level_rules *rules,
                                                         double seconds, bool pressed, bool same_level);
qa_q1_intermission_result qa_q1_level_rules_client_connected(qa_q1_level_rules *rules,
                                                             double seconds, bool same_level);
qa_q1_intermission_result qa_q1_level_rules_advance_finale(qa_q1_level_rules *rules, double seconds);
void qa_q1_level_rules_defer_exit(qa_q1_level_rules *rules, double until_seconds);

#endif
