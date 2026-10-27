/* Port of src/content/q3/base/game/hitscan.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_HITSCAN_H
#define QA_CONTENT_Q3_BASE_GAME_HITSCAN_H
#include <stdbool.h>
#include "qa/content/q3/base/game/ballistics_math.h"
#include "qa/content/q3/base/game/projectile.h"
typedef struct qa_q3_accuracy_subject {
  qa_actor_id actor; bool damageable, player; double health; const char *team; /* NULL = TS null */
} qa_q3_accuracy_subject;
bool qa_q3_accuracy_hit(bool team_game, qa_q3_accuracy_subject target, qa_q3_accuracy_subject attacker);
typedef struct qa_q3_bullet_attack { qa_vec3 forward, right, up, muzzle; double quad; } qa_q3_bullet_attack;
typedef qa_q3_projectile_target qa_q3_bullet_target;
typedef enum qa_q3_invuln_kind { QA_Q3_INVULN_MISS, QA_Q3_INVULN_HIT } qa_q3_invuln_kind;
typedef struct qa_q3_invuln_impact { qa_q3_invuln_kind kind; qa_vec3 impact_point, bounce_direction; } qa_q3_invuln_impact;
typedef struct qa_q3_bullet_event { qa_vec3 point, normal; bool has_target; qa_actor_id target; bool flesh; } qa_q3_bullet_event;
typedef struct qa_q3_bullet_host {
  qa_q3_game_product product; qa_q3_game_random_pick random; void *ctx;
  qa_q3_actor_trace_result_mirror (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_bullet_target *out);
  void (*impact)(void *ctx, qa_vec3 point); void (*emit)(void *ctx, qa_q3_bullet_event event);
  void (*damage)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point, double amount);
  void (*credit_accuracy_hit)(void *ctx);
  qa_q3_invuln_impact (*invulnerability_impact)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
} qa_q3_bullet_host;
void qa_q3_bullet_fire(qa_q3_bullet_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack, double spread, double amount);
typedef enum qa_q3_contact_event_kind { QA_Q3_CONTACT_HIT, QA_Q3_CONTACT_MISS, QA_Q3_CONTACT_LIGHTNING_REFLECTION, QA_Q3_CONTACT_GAUNTLET_QUAD } qa_q3_contact_event_kind;
typedef struct qa_q3_contact_event {
  qa_q3_contact_event_kind kind; qa_vec3 point, normal, start, end; qa_actor_id target;
} qa_q3_contact_event;
typedef struct qa_q3_contact_host {
  qa_q3_game_product product; void *ctx;
  qa_q3_actor_trace_result_mirror (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_bullet_target *out);
  void (*impact)(void *ctx, qa_vec3 point); void (*emit)(void *ctx, qa_q3_contact_event event);
  void (*damage)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point, double amount);
  void (*credit_accuracy_hit)(void *ctx);
  qa_q3_invuln_impact (*invulnerability_impact)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
} qa_q3_contact_host;
bool qa_q3_gauntlet_attack(qa_q3_contact_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack, bool quad_active);
void qa_q3_lightning_fire(qa_q3_contact_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack);
typedef struct qa_q3_shotgun_event { qa_vec3 muzzle, direction; double seed; } qa_q3_shotgun_event;
typedef struct qa_q3_shotgun_host {
  qa_q3_game_product product; void *ctx; qa_game_random *random;
  qa_q3_actor_trace_result_mirror (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_bullet_target *out);
  void (*impact)(void *ctx, qa_vec3 point);
  void (*damage)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point, double amount);
  void (*credit_accuracy_hit)(void *ctx);
  qa_q3_invuln_impact (*invulnerability_impact)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
  /* TS begin(muzzle,direction) returns publish(seed) closure: modelled as begin+publish pair. */
  void (*begin)(void *ctx, qa_vec3 muzzle, qa_vec3 direction); void (*publish)(void *ctx, double seed);
  bool (*alive)(void *ctx);
} qa_q3_shotgun_host;
void qa_q3_shotgun_fire(qa_q3_shotgun_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack);
typedef enum qa_q3_rail_impact_kind { QA_Q3_RAIL_NONE, QA_Q3_RAIL_SURFACE } qa_q3_rail_impact_kind;
typedef struct qa_q3_rail_trail { qa_vec3 start, end; qa_q3_rail_impact_kind impact_kind; qa_vec3 impact_normal; } qa_q3_rail_trail;
typedef struct qa_q3_rail_host {
  qa_q3_game_product product; void *ctx;
  qa_q3_actor_trace_result_mirror (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*target)(void *ctx, qa_actor_id actor, qa_q3_bullet_target *out);
  void (*impact)(void *ctx, qa_vec3 point);
  void (*damage)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point, double amount);
  qa_q3_invuln_impact (*invulnerability_impact)(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point);
  bool (*alive)(void *ctx);
  /* TS unlink returns restore closure or null: unlink returns token, restore(token) replays. */
  void *(*unlink)(void *ctx, qa_actor_id actor); void (*restore)(void *ctx, void *token);
  void (*trail)(void *ctx, qa_q3_rail_trail event);
} qa_q3_rail_host;
double qa_q3_rail_fire(qa_q3_rail_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack);
typedef struct qa_q3_rail_statistics { double streak, hits, impressive_count, reward_until; } qa_q3_rail_statistics;
typedef struct qa_q3_rail_statistics_result { qa_q3_rail_statistics state; bool awarded; } qa_q3_rail_statistics_result;
/* Getter-style name avoids clashing with type qa_q3_rail_statistics. */
qa_q3_rail_statistics_result qa_q3_update_rail_statistics(qa_q3_rail_statistics state, double hits, double time);
#endif
