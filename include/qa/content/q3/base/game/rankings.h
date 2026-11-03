/* Port of src/content/q3/base/game/rankings.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_RANKINGS_H
#define QA_CONTENT_Q3_BASE_GAME_RANKINGS_H
#include <stdbool.h>
typedef enum qa_q3_ranking_report_kind { QA_Q3_RANK_INTEGER, QA_Q3_RANK_STRING } qa_q3_ranking_report_kind;
typedef struct qa_q3_ranking_report {
  qa_q3_ranking_report_kind kind; double self, other, key, value_number; bool accumulate; const char *value_string;
} qa_q3_ranking_report;
typedef struct qa_q3_ranking_reports {
  void *ctx; void (*sink)(void *ctx, qa_q3_ranking_report report); bool (*warmup)(void *ctx); bool attached;
  bool has_last_hit; double last_hit_frame, last_hit_self, last_hit_attacker, last_hit_mod; /* TS lastHit string */
  double grapple_death; /* TS grappleDeath: MOD_GRAPPLE is 23 in base Q3 and 28 under MISSIONPACK, where MOD_NAIL takes 23. */
} qa_q3_ranking_reports;
void qa_q3_ranking_reports_init(qa_q3_ranking_reports *reports);
/* TS attach returns detach closure: call qa_q3_ranking_reports_detach. */
void qa_q3_ranking_reports_attach(qa_q3_ranking_reports *reports, void *ctx,
    void (*sink)(void *ctx, qa_q3_ranking_report report), bool (*warmup)(void *ctx));
void qa_q3_ranking_reports_detach(qa_q3_ranking_reports *reports);
void qa_q3_ranking_reports_integer(qa_q3_ranking_reports *r, double self, double other, double key, double value, bool accumulate);
void qa_q3_ranking_reports_string(qa_q3_ranking_reports *r, double self, double other, double key, const char *value);
void qa_q3_ranking_reports_fire_weapon(qa_q3_ranking_reports *r, double self, double weapon);
void qa_q3_ranking_reports_pickup_weapon(qa_q3_ranking_reports *r, double self, double weapon);
void qa_q3_ranking_reports_pickup_ammo(qa_q3_ranking_reports *r, double self, double weapon, double quantity);
void qa_q3_ranking_reports_pickup_holdable(qa_q3_ranking_reports *r, double self, double holdable);
void qa_q3_ranking_reports_pickup_powerup(qa_q3_ranking_reports *r, double self, double powerup);
void qa_q3_ranking_reports_pickup_armor(qa_q3_ranking_reports *r, double self, double quantity);
void qa_q3_ranking_reports_pickup_health(qa_q3_ranking_reports *r, double self, double quantity);
void qa_q3_ranking_reports_use_holdable(qa_q3_ranking_reports *r, double self, double holdable);
void qa_q3_ranking_reports_reward(qa_q3_ranking_reports *r, double self, double award);
void qa_q3_ranking_reports_capture(qa_q3_ranking_reports *r, double self);
void qa_q3_ranking_reports_damage(qa_q3_ranking_reports *r, double self, double attacker, double damage, double means_of_death, double frame, bool attacker_is_client, bool same_team);
void qa_q3_ranking_reports_player_die(qa_q3_ranking_reports *r, double self, double attacker, double means_of_death);
void qa_q3_ranking_reports_weapon_time(qa_q3_ranking_reports *r, double self, double weapon, double time);
void qa_q3_ranking_reports_team_name(qa_q3_ranking_reports *r, double self, const char *name);
#endif
