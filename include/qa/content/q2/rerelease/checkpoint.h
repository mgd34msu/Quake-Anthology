/* Port of src/content/q2/rerelease/checkpoint.ts (types only).
 * GPL-2.0-or-later. Arrays are borrowed slices; player states are
 * value copies (help_points borrowed from the source state). */
#ifndef QA_CONTENT_Q2_RERELEASE_CHECKPOINT_H
#define QA_CONTENT_Q2_RERELEASE_CHECKPOINT_H
#include <stddef.h>
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/goals.h"
#include "qa/content/q2/rerelease/q64/index.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/session.h"

typedef struct qa_q2rr_saved_player { qa_saved_actor_id actor; qa_q2rr_player_extra_state state; } qa_q2rr_saved_player;
typedef struct qa_q2rr_saved_placement { qa_saved_actor_id actor; qa_vec3 origin, angles; } qa_q2rr_saved_placement;
typedef struct qa_q2_rerelease_players_checkpoint {
  double version; /* 1 */
  qa_q2_rerelease_options options;
  double coop_restart_time;
  bool deadly_kill_box;
  double intermission_flags;
  bool has_intermission_fade_until; double intermission_fade_until;
  bool has_intermission_camera; qa_vec3 camera_origin, camera_angles;
  bool intermission_camera_set;
  const qa_q2rr_saved_player *players; size_t player_count;
  const qa_q2rr_saved_placement *squad_spawns; size_t squad_count;
} qa_q2_rerelease_players_checkpoint;

typedef struct qa_q2rr_sky { const char *name; double rotation; bool auto_rotate; qa_vec3 axis; } qa_q2rr_sky;
typedef struct qa_q2rr_saved_poi { qa_saved_actor_id actor; qa_vec3 origin; const char *image; bool has_dynamic; qa_saved_actor_id dynamic; } qa_q2rr_saved_poi;
typedef struct qa_q2rr_campaign_checkpoint {
  double cross_unit_flags;
  const char *const *visited_maps; size_t visited_count;
  const qa_q2_rerelease_level_entry *levels; size_t level_count;
  qa_q2rr_mission mission;
} qa_q2rr_campaign_checkpoint;
typedef struct qa_q2rr_saved_healthbar { bool has; qa_saved_actor_id controller, target; bool has_dead_until; double dead_until; } qa_q2rr_saved_healthbar;
typedef struct qa_q2rr_saved_pair { qa_saved_actor_id controller, target; } qa_q2rr_saved_pair;
typedef struct qa_q2rr_saved_slots { qa_saved_actor_id actor; const double *slots; size_t slot_count; } qa_q2rr_saved_slots;
typedef struct qa_q2rr_saved_time { qa_saved_actor_id actor; double time; } qa_q2rr_saved_time;
typedef struct qa_q2rr_saved_light { qa_saved_actor_id actor; bool active; } qa_q2rr_saved_light;
typedef struct qa_q2_rerelease_module_checkpoint {
  double version; /* 1 */
  qa_q2_fog_state world_fog;
  const char *story;
  qa_q2rr_sky sky;
  bool has_poi; qa_q2rr_saved_poi poi;
  double poi_stage, last_auto_save;
  qa_q2rr_campaign_checkpoint campaign;
  const qa_q2rr_saved_healthbar *health_bars; size_t health_bar_count;
  const qa_q2rr_saved_pair *health_targets; size_t health_target_count;
  const qa_q2rr_saved_slots *picked_up_by; size_t picked_count;
  const qa_q2rr_saved_time *trigger_sound_times; size_t trigger_sound_count;
  qa_q2_rerelease_q64_checkpoint q64;
  const qa_q2rr_saved_light *lights; size_t light_count;
  qa_q2_rerelease_goals_checkpoint goals;
} qa_q2_rerelease_module_checkpoint;
#endif
