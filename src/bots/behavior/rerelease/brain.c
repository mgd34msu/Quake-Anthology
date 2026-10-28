/* Port of src/bots/behavior/rerelease/brain.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/brain.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUCK_SECONDS 0.6
#define REPLAN_SECONDS 2.0
#define STUCK_GIVE_UP 3
#define ROAM_RADIUS 4096
#define EDGE_LOOKAHEAD 32
#define EDGE_STOP_SECONDS 0.15
#define EDGE_PROBE_STEP 16
#define EDGE_DROP_CHECK 1024
#define JUMP_AIR_SECONDS 0.7
#define JUMP_VELOCITY 270
#define BRAIN_GRAVITY 800
#define GAP_LANDING_SEARCH 320
#define GAP_LANDING_RISE 40
#define GAP_JUMP_TRIGGER 32
#define GAP_REACH_MARGIN 0.85
#define LAVA_EXIT_SAMPLES 12
#define LAVA_EXIT_REACH 96
#define POINT_REST_SECONDS 8
#define ESCORT_DISTANCE 160
#define UNREACHABLE_SECONDS 20
#define UNREACHABLE_LIVE_SECONDS 4
#define UNSTICK_SECONDS 0.5
#define INTERACT_RADIUS 1024
#define INTERACT_SECONDS 8
#define INTERACT_REACHED 40
#define WEDGED_SECONDS 1.2
#define WEDGED_GIVE_UP_SECONDS 2.5
#define WEDGED_DISPLACEMENT 96
#define BLIND_ROAM_RADIUS 640
#define BLIND_ROAM_SECONDS 4
#define COOP_REGROUP_SECONDS 15
#define COOP_REGROUP_NEAR 250
#define COOP_REGROUP_GIVE_UP 12
#define COOP_HUNT_RADIUS 2000
#define OBJECTIVE_GUARD_RADIUS 384
#define CARRIER_HOLD_RADIUS 192
#define UNSTICK_STEP 40
#define UNSTICK_MAX_DROP 96
#define SURFACE_REACH 96
#define TOUCH_RADIUS 12
#define GATE_SHOOT_RANGE 768
#define GATE_AIM_DEGREES 5
#define GATE_SHOT_SECONDS 0.7
#define OBJECTIVE_AWAY 96

static char *dups(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p;
}
static qa_bot_vec3 v3(double x, double y, double z) { return (qa_bot_vec3){x, y, z}; }

/* awareness map (insertion order) */
static qa_bot_awareness *awareness_get(qa_bot_brain *brain, int64_t id) {
  for (size_t i = 0; i < brain->state.awareness_count; i++)
    if (brain->state.awareness[i].id == id) return &brain->state.awareness[i];
  return NULL;
}
static qa_bot_awareness *awareness_ensure(qa_bot_brain *brain, int64_t id, double now, qa_bot_vec3 origin) {
  qa_bot_awareness *aw = awareness_get(brain, id);
  if (aw) return aw;
  if (brain->state.awareness_count == brain->state.awareness_cap) {
    size_t c = brain->state.awareness_cap ? brain->state.awareness_cap * 2 : 8;
    qa_bot_awareness *p = realloc(brain->state.awareness, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    brain->state.awareness = p; brain->state.awareness_cap = c;
  }
  brain->state.awareness[brain->state.awareness_count++] = qa_new_awareness(id, now, origin);
  return &brain->state.awareness[brain->state.awareness_count - 1];
}
static void awareness_delete(qa_bot_brain *brain, int64_t id) {
  for (size_t i = 0; i < brain->state.awareness_count; i++)
    if (brain->state.awareness[i].id == id) {
      memmove(&brain->state.awareness[i], &brain->state.awareness[i + 1],
              (brain->state.awareness_count - i - 1) * sizeof *brain->state.awareness);
      brain->state.awareness_count--;
      return;
    }
}
static bool unreach_get(qa_bot_brain *brain, int64_t id, double *out) {
  for (size_t i = 0; i < brain->state.unreachable_count; i++)
    if (brain->state.unreachable_until[i].entity == id) { *out = brain->state.unreachable_until[i].time; return true; }
  return false;
}
static void unreach_set(qa_bot_brain *brain, int64_t id, double time) {
  for (size_t i = 0; i < brain->state.unreachable_count; i++)
    if (brain->state.unreachable_until[i].entity == id) { brain->state.unreachable_until[i].time = time; return; }
  if (brain->state.unreachable_count == brain->state.unreachable_cap) {
    size_t c = brain->state.unreachable_cap ? brain->state.unreachable_cap * 2 : 8;
    qa_bot_id_time *p = realloc(brain->state.unreachable_until, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    brain->state.unreachable_until = p; brain->state.unreachable_cap = c;
  }
  brain->state.unreachable_until[brain->state.unreachable_count++] = (qa_bot_id_time){id, time};
}
static void unreach_delete(qa_bot_brain *brain, int64_t id) {
  for (size_t i = 0; i < brain->state.unreachable_count; i++)
    if (brain->state.unreachable_until[i].entity == id) {
      memmove(&brain->state.unreachable_until[i], &brain->state.unreachable_until[i + 1],
              (brain->state.unreachable_count - i - 1) * sizeof *brain->state.unreachable_until);
      brain->state.unreachable_count--;
      return;
    }
}
static bool objhome_get(qa_bot_brain *brain, int64_t id, qa_bot_vec3 *out) {
  for (size_t i = 0; i < brain->state.objective_home_count; i++)
    if (brain->state.objective_home[i].entity == id) { *out = brain->state.objective_home[i].origin; return true; }
  return false;
}
static void objhome_set(qa_bot_brain *brain, int64_t id, qa_bot_vec3 origin) {
  for (size_t i = 0; i < brain->state.objective_home_count; i++)
    if (brain->state.objective_home[i].entity == id) { brain->state.objective_home[i].origin = origin; return; }
  if (brain->state.objective_home_count == brain->state.objective_home_cap) {
    size_t c = brain->state.objective_home_cap ? brain->state.objective_home_cap * 2 : 8;
    qa_bot_id_origin *p = realloc(brain->state.objective_home, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    brain->state.objective_home = p; brain->state.objective_home_cap = c;
  }
  brain->state.objective_home[brain->state.objective_home_count++] = (qa_bot_id_origin){id, origin};
}
static bool said_has(qa_bot_brain *brain, const char *type) {
  for (size_t i = 0; i < brain->state.said_count; i++) if (!strcmp(brain->state.said_this_level[i], type)) return true;
  return false;
}
static void said_add(qa_bot_brain *brain, const char *type) {
  if (said_has(brain, type)) return;
  if (brain->state.said_count == brain->state.said_cap) {
    size_t c = brain->state.said_cap ? brain->state.said_cap * 2 : 8;
    char **p = realloc(brain->state.said_this_level, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    brain->state.said_this_level = p; brain->state.said_cap = c;
  }
  char *copy = dups(type);
  if (!copy) qa_range_error("Out of memory");
  brain->state.said_this_level[brain->state.said_count++] = copy;
}
static bool flag_includes(const qa_bot_item_rec *item, const char *flag) {
  for (size_t i = 0; i < item->entry->flag_count; i++) if (!strcmp(item->entry->flags[i], flag)) return true;
  return false;
}
static bool weapon_flag_includes(const qa_bot_weapon_rec *weapon, const char *flag) {
  for (size_t i = 0; i < weapon->entry->flag_count; i++) if (!strcmp(weapon->entry->flags[i], flag)) return true;
  return false;
}
static void memory_init(qa_bot_brain_memory *s) {
  memset(s, 0, sizeof *s);
  s->trigger_held_since = -1;
  s->aim = qa_new_aim_state(0, 0);
  s->path_state = qa_new_path_state();
  s->target_id = -1;
  s->goal_entity_id = -1;
  s->wedge_since = -1;
  s->gate_fired_at = -1;
  s->dead_since = -1;
  s->objective_role = dups("");
  s->last_cmd = qa_empty_usercmd();
}
static void memory_clear_dynamic(qa_bot_brain_memory *s) {
  if (s->has_owned_path) qa_nav_path_destroy(&s->owned_path);
  s->has_owned_path = false;
  s->path_state.path = NULL;
  free(s->awareness); s->awareness = NULL; s->awareness_count = s->awareness_cap = 0;
  free(s->unreachable_until); s->unreachable_until = NULL; s->unreachable_count = s->unreachable_cap = 0;
  free(s->objective_home); s->objective_home = NULL; s->objective_home_count = s->objective_home_cap = 0;
  for (size_t i = 0; i < s->said_count; i++) free(s->said_this_level[i]);
  free(s->said_this_level); s->said_this_level = NULL; s->said_count = s->said_cap = 0;
  free(s->objective_role); s->objective_role = NULL;
}
static bool game_mode_copy(qa_bot_game_mode *dst, const qa_bot_game_mode *src) {
  qa_bot_game_mode_destroy(dst);
  *dst = *src;
  dst->game_type_owned = dups(src->game_type ? src->game_type : "");
  dst->game_type = dst->game_type_owned;
  return dst->game_type_owned != NULL;
}
bool qa_bot_brain_init(qa_bot_brain *brain, qa_bot_brain_config config) {
  memset(brain, 0, sizeof *brain);
  brain->config = config;
  brain->config.game_mode.game_type_owned = NULL;
  brain->config.game_mode.game_type = NULL;
  if (!game_mode_copy(&brain->config.game_mode, &config.game_mode)) return false;
  brain->config.skill = dups(config.skill ? config.skill : "");
  if (!brain->config.skill) { qa_bot_game_mode_destroy(&brain->config.game_mode); return false; }
  const qa_bot_skill_settings *settings = qa_bot_knowledge_get_skill(config.knowledge, brain->config.skill);
  if (!settings && config.knowledge->settings_r.skill_count > 0) settings = &config.knowledge->settings_r.skills[0];
  if (!settings)
    qa_range_error("bot_brain: no skill settings available (asked for \"%s\")", brain->config.skill);
  brain->settings = settings;
  memory_init(&brain->state);
  if (!brain->state.objective_role) { qa_bot_brain_destroy(brain); return false; }
  return true;
}
void qa_bot_brain_destroy(qa_bot_brain *brain) {
  if (!brain) return;
  memory_clear_dynamic(&brain->state);
  free((void *)brain->config.skill);
  qa_bot_game_mode_destroy(&brain->config.game_mode);
  memset(brain, 0, sizeof *brain);
}
static bool path_copy(qa_nav_path *dst, const qa_nav_path *src) {
  memset(dst, 0, sizeof *dst);
  dst->cost = src->cost; dst->generation = src->generation; dst->map_digest = src->map_digest;
  dst->node_count = src->node_count; dst->point_count = src->point_count; dst->link_count = src->link_count;
  if (src->node_count) { dst->nodes = malloc(src->node_count * sizeof *dst->nodes); if (!dst->nodes) goto oom; memcpy(dst->nodes, src->nodes, src->node_count * sizeof *dst->nodes); }
  if (src->point_count) { dst->points = malloc(src->point_count * sizeof *dst->points); if (!dst->points) goto oom; memcpy(dst->points, src->points, src->point_count * sizeof *dst->points); }
  if (src->link_count) {
    dst->links = malloc(src->link_count * sizeof *dst->links); if (!dst->links) goto oom; memcpy(dst->links, src->links, src->link_count * sizeof *dst->links);
    dst->has_link = malloc(src->link_count * sizeof *dst->has_link); if (!dst->has_link) goto oom; memcpy(dst->has_link, src->has_link, src->link_count * sizeof *dst->has_link);
  }
  return true;
oom:
  qa_nav_path_destroy(dst);
  return false;
}
void qa_bot_brain_checkpoint_destroy(qa_bot_brain_checkpoint *checkpoint) {
  if (!checkpoint) return;
  free(checkpoint->skill);
  qa_bot_game_mode_destroy(&checkpoint->game_mode);
  qa_bot_brain_checkpoint_memory *m = &checkpoint->memory;
  free(m->objective_role);
  if (m->has_path) qa_nav_path_destroy(&m->path);
  free(m->awareness); free(m->unreachable_until); free(m->objective_home);
  for (size_t i = 0; i < m->said_count; i++) free(m->said_this_level[i]);
  free(m->said_this_level);
  memset(checkpoint, 0, sizeof *checkpoint);
}
bool qa_bot_brain_checkpoint_make(qa_bot_brain *brain, qa_bot_brain_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_bot_brain_memory *s = &brain->state;
  out->version = 1;
  out->skill = dups(brain->settings->skill);
  if (!out->skill) goto oom;
  if (!game_mode_copy(&out->game_mode, &brain->config.game_mode)) goto oom;
  qa_bot_brain_checkpoint_memory *m = &out->memory;
  m->trigger_weapon = s->trigger_weapon; m->trigger_held_since = s->trigger_held_since; m->trigger_ready_at = s->trigger_ready_at;
  m->target_id = s->target_id; m->has_goal_point = s->has_goal_point; m->goal_point = s->goal_point;
  m->goal_entity_id = s->goal_entity_id; m->stuck_trips = s->stuck_trips; m->goal_is_live = s->goal_is_live;
  m->unstick_until = s->unstick_until; m->press_until = s->press_until; m->unstick_side = s->unstick_side;
  m->explicit_goal_done = s->explicit_goal_done; m->explicit_goal_failed = s->explicit_goal_failed;
  m->has_wedge_origin = s->has_wedge_origin; m->wedge_origin = s->wedge_origin; m->wedge_since = s->wedge_since;
  m->has_last_safe_origin = s->has_last_safe_origin; m->last_safe_origin = s->last_safe_origin;
  m->has_rest_point = s->has_rest_point; m->rest_point = s->rest_point; m->rest_until = s->rest_until;
  m->guard_refusals = s->guard_refusals; m->last_guard_refused = s->last_guard_refused;
  m->gap_jumps = s->gap_jumps; m->hazard_frames = s->hazard_frames;
  m->has_own_objective_home = s->has_own_objective_home; m->own_objective_home = s->own_objective_home;
  m->has_enemy_objective_home = s->has_enemy_objective_home; m->enemy_objective_home = s->enemy_objective_home;
  m->objective_role = dups(s->objective_role ? s->objective_role : "");
  if (!m->objective_role) goto oom;
  m->touch_goal = s->touch_goal; m->hold_position = s->hold_position;
  m->has_gate_shoot_at = s->has_gate_shoot_at; m->gate_shoot_at = s->gate_shoot_at; m->gate_fired_at = s->gate_fired_at;
  m->coop_regrouping = s->coop_regrouping; m->coop_regroup_at = s->coop_regroup_at; m->coop_regroup_until = s->coop_regroup_until;
  m->level_started = s->level_started; m->check_six_until = s->check_six_until; m->check_six_next_at = s->check_six_next_at;
  m->has_roam_point = s->has_roam_point; m->roam_point = s->roam_point; m->roam_until = s->roam_until;
  m->spawned_once = s->spawned_once; m->last_weapon_number = s->last_weapon_number;
  m->dead_since = s->dead_since; m->respawn_wait = s->respawn_wait; m->respawn_press = s->respawn_press;
  m->aim = s->aim;
  m->has_path = s->path_state.path != NULL;
  if (m->has_path && !path_copy(&m->path, s->path_state.path)) goto oom;
  m->path_index = s->path_state.index; m->stuck_origin = s->path_state.stuck_origin; m->stuck_since = s->path_state.stuck_since;
  m->stuck_count = s->path_state.stuck_count; m->lift_wait_since = s->path_state.lift_wait_since; m->lift_wait_z = s->path_state.lift_wait_z;
  m->jump_ready_at = s->path_state.jump_ready_at; m->planned_at = s->path_state.planned_at;
  m->awareness_count = s->awareness_count;
  if (m->awareness_count) {
    m->awareness = malloc(m->awareness_count * sizeof *m->awareness); if (!m->awareness) goto oom;
    for (size_t i = 0; i < m->awareness_count; i++) m->awareness[i] = (qa_bot_awareness_entry){s->awareness[i].id, s->awareness[i]};
  }
  m->unreachable_count = s->unreachable_count;
  if (m->unreachable_count) {
    m->unreachable_until = malloc(m->unreachable_count * sizeof *m->unreachable_until); if (!m->unreachable_until) goto oom;
    memcpy(m->unreachable_until, s->unreachable_until, m->unreachable_count * sizeof *m->unreachable_until);
  }
  m->objective_home_count = s->objective_home_count;
  if (m->objective_home_count) {
    m->objective_home = malloc(m->objective_home_count * sizeof *m->objective_home); if (!m->objective_home) goto oom;
    memcpy(m->objective_home, s->objective_home, m->objective_home_count * sizeof *m->objective_home);
  }
  m->said_count = s->said_count;
  if (m->said_count) {
    m->said_this_level = calloc(m->said_count, sizeof *m->said_this_level); if (!m->said_this_level) goto oom;
    for (size_t i = 0; i < m->said_count; i++) { m->said_this_level[i] = dups(s->said_this_level[i]); if (!m->said_this_level[i]) goto oom; }
  }
  m->has_explicit_goal = s->has_explicit_goal; m->explicit_goal = s->explicit_goal;
  m->last_cmd = s->last_cmd;
  return true;
oom:
  qa_bot_brain_checkpoint_destroy(out);
  return false;
}
bool qa_bot_brain_restore(qa_bot_brain *brain, const qa_bot_brain_checkpoint *checkpoint) {
  if (checkpoint->version != 1 || !checkpoint->skill || strcmp(checkpoint->skill, brain->settings->skill))
    qa_range_error("Bot behavior checkpoint has another source skill/profile version");
  const qa_bot_brain_checkpoint_memory *m = &checkpoint->memory;
  qa_bot_brain_memory fresh;
  memory_init(&fresh);
  fresh.trigger_weapon = m->trigger_weapon; fresh.trigger_held_since = m->trigger_held_since; fresh.trigger_ready_at = m->trigger_ready_at;
  fresh.aim = m->aim;
  fresh.path_state.index = m->path_index; fresh.path_state.stuck_origin = m->stuck_origin; fresh.path_state.stuck_since = m->stuck_since;
  fresh.path_state.stuck_count = m->stuck_count; fresh.path_state.lift_wait_since = m->lift_wait_since; fresh.path_state.lift_wait_z = m->lift_wait_z;
  fresh.path_state.jump_ready_at = m->jump_ready_at; fresh.path_state.planned_at = m->planned_at;
  if (m->has_path) {
    if (!path_copy(&fresh.owned_path, &m->path)) { memory_clear_dynamic(&fresh); return false; }
    fresh.has_owned_path = true;
    fresh.path_state.path = &fresh.owned_path;
  }
  fresh.target_id = m->target_id; fresh.has_goal_point = m->has_goal_point; fresh.goal_point = m->goal_point;
  fresh.goal_entity_id = m->goal_entity_id; fresh.stuck_trips = m->stuck_trips; fresh.goal_is_live = m->goal_is_live;
  fresh.unstick_until = m->unstick_until; fresh.press_until = m->press_until; fresh.unstick_side = m->unstick_side;
  fresh.has_explicit_goal = m->has_explicit_goal; fresh.explicit_goal = m->explicit_goal;
  fresh.explicit_goal_done = m->explicit_goal_done; fresh.explicit_goal_failed = m->explicit_goal_failed;
  fresh.has_wedge_origin = m->has_wedge_origin; fresh.wedge_origin = m->wedge_origin; fresh.wedge_since = m->wedge_since;
  fresh.has_last_safe_origin = m->has_last_safe_origin; fresh.last_safe_origin = m->last_safe_origin;
  fresh.has_rest_point = m->has_rest_point; fresh.rest_point = m->rest_point; fresh.rest_until = m->rest_until;
  fresh.guard_refusals = m->guard_refusals; fresh.last_guard_refused = m->last_guard_refused;
  fresh.gap_jumps = m->gap_jumps; fresh.hazard_frames = m->hazard_frames;
  fresh.has_own_objective_home = m->has_own_objective_home; fresh.own_objective_home = m->own_objective_home;
  fresh.has_enemy_objective_home = m->has_enemy_objective_home; fresh.enemy_objective_home = m->enemy_objective_home;
  free(fresh.objective_role);
  fresh.objective_role = dups(m->objective_role ? m->objective_role : "");
  if (!fresh.objective_role) { memory_clear_dynamic(&fresh); return false; }
  fresh.touch_goal = m->touch_goal; fresh.hold_position = m->hold_position;
  fresh.has_gate_shoot_at = m->has_gate_shoot_at; fresh.gate_shoot_at = m->gate_shoot_at; fresh.gate_fired_at = m->gate_fired_at;
  fresh.coop_regrouping = m->coop_regrouping; fresh.coop_regroup_at = m->coop_regroup_at; fresh.coop_regroup_until = m->coop_regroup_until;
  fresh.level_started = m->level_started; fresh.check_six_until = m->check_six_until; fresh.check_six_next_at = m->check_six_next_at;
  fresh.has_roam_point = m->has_roam_point; fresh.roam_point = m->roam_point; fresh.roam_until = m->roam_until;
  fresh.last_cmd = m->last_cmd; fresh.spawned_once = m->spawned_once; fresh.last_weapon_number = m->last_weapon_number;
  fresh.dead_since = m->dead_since; fresh.respawn_wait = m->respawn_wait; fresh.respawn_press = m->respawn_press;
  /* maps: later entries replace earlier (TS Map construction) */
  qa_bot_brain probe; memset(&probe, 0, sizeof probe); probe.state = fresh;
  for (size_t i = 0; i < m->awareness_count; i++) {
    qa_bot_awareness *aw = awareness_ensure(&probe, m->awareness[i].entity, 0, m->awareness[i].value.last_known_origin);
    *aw = m->awareness[i].value;
  }
  for (size_t i = 0; i < m->unreachable_count; i++) unreach_set(&probe, m->unreachable_until[i].entity, m->unreachable_until[i].time);
  for (size_t i = 0; i < m->objective_home_count; i++) objhome_set(&probe, m->objective_home[i].entity, m->objective_home[i].origin);
  for (size_t i = 0; i < m->said_count; i++) said_add(&probe, m->said_this_level[i]);
  fresh = probe.state;
  if (!game_mode_copy(&brain->config.game_mode, &checkpoint->game_mode)) { memory_clear_dynamic(&fresh); return false; }
  memory_clear_dynamic(&brain->state);
  brain->state = fresh;
  return true;
}

static void set_point_goal(qa_bot_brain *brain, qa_bot_vec3 point, bool objective_owner) {
  qa_bot_brain_memory *s = &brain->state;
  bool same = s->has_explicit_goal && s->explicit_goal.objective_owner == objective_owner && !s->explicit_goal.is_entity &&
              qa_bvec_distance(s->explicit_goal.point, point) < 8;
  if (same) return;
  s->explicit_goal = (qa_bot_explicit_goal){objective_owner, false, point, -1};
  s->has_explicit_goal = true;
  s->explicit_goal_done = false;
  s->explicit_goal_failed = false;
  qa_bot_clear_path(&s->path_state);
}
void qa_bot_brain_request_move_to_point(qa_bot_brain *brain, qa_bot_vec3 point) { set_point_goal(brain, point, false); }
void qa_bot_brain_set_objective_goal(qa_bot_brain *brain, const qa_bot_vec3 *point) {
  qa_bot_brain_memory *s = &brain->state;
  if (s->has_explicit_goal && !s->explicit_goal.objective_owner) return;
  if (!point) {
    if (s->has_explicit_goal && s->explicit_goal.objective_owner) qa_bot_brain_clear_explicit_goal(brain);
    return;
  }
  set_point_goal(brain, *point, true);
}
void qa_bot_brain_request_follow_entity(qa_bot_brain *brain, double entity_id, qa_bot_vec3 origin) {
  qa_bot_brain_memory *s = &brain->state;
  if (s->has_explicit_goal && s->explicit_goal.is_entity && (double)s->explicit_goal.entity_id == entity_id) {
    s->explicit_goal.point = origin;
    return;
  }
  s->explicit_goal = (qa_bot_explicit_goal){false, true, origin, (int64_t)entity_id};
  s->has_explicit_goal = true;
  s->explicit_goal_done = false;
  s->explicit_goal_failed = false;
  qa_bot_clear_path(&s->path_state);
}
int64_t qa_bot_brain_goal_status(qa_bot_brain *brain) {
  if (!brain->state.has_explicit_goal) return QA_BOT_GOAL_ERROR;
  if (brain->state.explicit_goal_failed) return QA_BOT_GOAL_ERROR;
  if (brain->state.explicit_goal_done) return QA_BOT_GOAL_SUCCESS;
  return QA_BOT_GOAL_IN_PROGRESS;
}
void qa_bot_brain_clear_explicit_goal(qa_bot_brain *brain) {
  qa_bot_brain_memory *s = &brain->state;
  if (s->has_explicit_goal) {
    qa_bot_clear_path(&s->path_state);
    s->has_goal_point = false;
    s->goal_entity_id = -1;
  }
  s->has_explicit_goal = false;
  s->explicit_goal_done = false;
  s->explicit_goal_failed = false;
}
void qa_bot_brain_reset_for_level(qa_bot_brain *brain) {
  qa_bot_brain_memory *s = &brain->state;
  s->trigger_weapon = 0; s->trigger_held_since = -1; s->trigger_ready_at = 0;
  qa_bot_clear_path(&s->path_state);
  s->awareness_count = 0;
  qa_bot_brain_clear_explicit_goal(brain);
  s->target_id = -1;
  s->has_goal_point = false;
  s->goal_entity_id = -1;
  s->unreachable_count = 0;
  s->stuck_trips = 0;
  s->unstick_until = 0;
  s->press_until = 0;
  s->has_roam_point = false;
  s->roam_until = 0;
  s->check_six_until = 0;
  s->check_six_next_at = 0;
  s->dead_since = -1;
  s->last_weapon_number = 0;
  s->last_cmd = qa_empty_usercmd();
  s->has_wedge_origin = false;
  s->has_last_safe_origin = false;
  s->has_rest_point = false;
  s->rest_until = 0;
  s->wedge_since = -1;
  s->objective_home_count = 0;
  s->has_own_objective_home = false;
  s->has_enemy_objective_home = false;
  free(s->objective_role); s->objective_role = dups("");
  if (!s->objective_role) qa_range_error("Out of memory");
  s->coop_regrouping = false;
  s->coop_regroup_at = 0;
  s->coop_regroup_until = 0;
  for (size_t i = 0; i < s->said_count; i++) free(s->said_this_level[i]);
  s->said_count = 0;
  s->level_started = false;
}
bool qa_bot_brain_set_game_mode(qa_bot_brain *brain, qa_bot_game_mode mode) {
  return game_mode_copy(&brain->config.game_mode, &mode);
}
const qa_bot_awareness *qa_bot_brain_awareness_of(qa_bot_brain *brain, int64_t id) { return awareness_get(brain, id); }
double qa_bot_brain_current_target(qa_bot_brain *brain) { return brain->state.target_id; }
const qa_nav_path *qa_bot_brain_current_path(qa_bot_brain *brain) { return brain->state.path_state.path; }
int64_t qa_bot_brain_current_path_index(qa_bot_brain *brain) { return brain->state.path_state.index; }
qa_bot_usercmd qa_bot_brain_last_usercmd(qa_bot_brain *brain) { return brain->state.last_cmd; }
void qa_bot_brain_emit_chat(qa_bot_brain *brain, const char *type) {
  if (!brain->config.on_chat) return;
  size_t count = 0;
  const qa_chat_entry **candidates = qa_bot_knowledge_chats_of_type(brain->config.knowledge, type, &count);
  if (!candidates) qa_range_error("Out of memory");
  if (count == 0) { free((void *)candidates); return; }
  int64_t index = qa_bot_random_index(brain->config.rng, (int64_t)count);
  if (index < 0 || index >= (int64_t)count) { free((void *)candidates); qa_range_error("Bot chat selection is outside its source candidates"); }
  const qa_chat_entry *chat = candidates[index];
  qa_bot_chat_event event = {chat->locstring, chat->type, chat->time, chat->team};
  double chance = chat->chance;
  free((void *)candidates);
  if (!qa_bot_random_chance(brain->config.rng, chance)) return;
  brain->config.on_chat(brain->config.on_chat_ctx, event);
}
static void emit_chat_once(qa_bot_brain *brain, const char *type) {
  if (said_has(brain, type)) return;
  said_add(brain, type);
  qa_bot_brain_emit_chat(brain, type);
}
static double weapon_cone(qa_bot_brain *brain, double weapon_number) {
  if (brain->settings->weapons.fov_scalar <= 0) return brain->settings->weapons.fov_angle;
  const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(brain->config.knowledge, weapon_number);
  double ideal = weapon && weapon->entry->ideal_fov > 0 ? weapon->entry->ideal_fov : brain->settings->senses.fov_angle;
  return ideal * brain->settings->weapons.fov_scalar;
}
static bool coop_game(qa_bot_brain *brain) {
  const char *type = brain->config.game_mode.game_type;
  return type && (!strcmp(type, QA_BOT_GAME_TYPE_COOP) || !strcmp(type, QA_BOT_GAME_TYPE_HORDE));
}
static bool team_game(qa_bot_brain *brain) {
  if (brain->config.game_mode.has_has_teams) return brain->config.game_mode.has_teams;
  const char *type = brain->config.game_mode.game_type;
  return (type && (!strcmp(type, QA_BOT_GAME_TYPE_TDM) || !strcmp(type, QA_BOT_GAME_TYPE_CTF))) || coop_game(brain);
}
static bool friendly(qa_bot_brain *brain, const qa_bot_entity *ent, int64_t team) {
  if (ent->kind == QA_BOT_ENTITY_MONSTER)
    return qa_bot_knowledge_get_monster(brain->config.knowledge, ent->classname ? ent->classname : "") == NULL;
  if (ent->kind != QA_BOT_ENTITY_PLAYER) return true;
  if (!team_game(brain)) return false;
  if (team <= 0 || ent->team <= 0) return false;
  return ent->team == team;
}
static void update_senses(qa_bot_brain *brain, qa_bot_world *world, qa_bot_self self,
                          const qa_bot_entity *entities, size_t entity_count,
                          const qa_bot_sound *sounds, size_t sound_count, double dt, double now) {
  qa_bot_senses_settings senses = brain->settings->senses;
  qa_bot_weapon_sense_settings weapons = brain->settings->weapons;
  weapons.fov_angle = weapon_cone(brain, (double)self.current_weapon);
  for (size_t i = 0; i < entity_count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->id == self.id) continue;
    if (ent->kind != QA_BOT_ENTITY_PLAYER && ent->kind != QA_BOT_ENTITY_MONSTER) continue;
    if (ent->dead) { awareness_delete(brain, ent->id); continue; }
    qa_sight_geometry geom = qa_evaluate_sight_geometry(self.eye, brain->state.aim.pitch, brain->state.aim.yaw,
                                                        ent->center, ent->invisible, senses, weapons);
    qa_bot_trace sight = world->trace_line(world->ctx, self.eye, ent->center);
    bool clear = geom.within_invis_range && (sight.fraction >= 1 || sight.hit_id == ent->id);
    bool audible = false;
    for (size_t j = 0; j < sound_count; j++) {
      if (sounds[j].source_id != ent->id) continue;
      if (qa_bot_sound_audible(sounds[j], self.origin, senses, now)) { audible = true; break; }
    }
    qa_bot_contact contact = {clear, geom.in_sight_fov, geom.in_weapon_fov, audible, ent->invisible, geom.distance, ent->center};
    qa_bot_awareness *aw = awareness_ensure(brain, ent->id, now, ent->center);
    qa_sense_step(aw, contact, senses, weapons, dt, now);
  }
  size_t i = 0;
  while (i < brain->state.awareness_count) {
    if (qa_bot_should_forget(&brain->state.awareness[i], senses, now)) awareness_delete(brain, brain->state.awareness[i].id);
    else i++;
  }
}
static const qa_bot_entity *select_target(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count,
                                          int64_t team, qa_bot_vec3 self_origin) {
  if (!brain->settings->behaviors.allow_combat) {
    for (size_t i = 0; i < count; i++) {
      qa_bot_awareness *aw = awareness_get(brain, entities[i].id);
      if (aw && qa_bot_is_aware(aw) && !friendly(brain, &entities[i], team)) return &entities[i];
    }
    return NULL;
  }
  const qa_bot_entity *best = NULL;
  double best_score = -INFINITY;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    qa_bot_awareness *aw = awareness_get(brain, ent->id);
    if (!aw || !qa_bot_is_aware(aw)) continue;
    if (friendly(brain, ent, team)) continue;
    double score = 4096 - qa_bvec_distance(self_origin, aw->last_known_origin);
    if (ent->kind == QA_BOT_ENTITY_PLAYER) score += 512;
    if (ent->has_carrying_objective && ent->carrying_objective) score += 1536;
    score += aw->weapon * 256;
    if ((double)ent->id == brain->state.target_id) score += 128;
    if (score > best_score) { best_score = score; best = ent; }
  }
  return best;
}
static bool update_wedge(qa_bot_brain *brain, qa_bot_vec3 origin, double now, bool in_combat) {
  qa_bot_brain_memory *s = &brain->state;
  bool pressing = s->last_cmd.forwardmove != 0 || s->last_cmd.sidemove != 0 || s->last_guard_refused;
  if (in_combat || !s->has_wedge_origin || !pressing || qa_bvec_distance(origin, s->wedge_origin) > WEDGED_DISPLACEMENT) {
    s->wedge_origin = origin; s->has_wedge_origin = true;
    s->wedge_since = now;
    return false;
  }
  return now - s->wedge_since >= WEDGED_SECONDS;
}
static void update_objectives(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_self self, double now) {
  qa_bot_brain_memory *s = &brain->state;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_ITEM) continue;
    const qa_bot_item_rec *item = qa_bot_knowledge_get_item(brain->config.knowledge, ent->classname ? ent->classname : "");
    if (!item || !flag_includes(item, QA_ITEM_FLAG_OBJECTIVE)) continue;
    qa_bot_vec3 home;
    if (!objhome_get(brain, ent->id, &home)) { objhome_set(brain, ent->id, ent->origin); home = ent->origin; }
    int64_t team = item->entry->has_team ? (int64_t)item->entry->team : ent->team;
    if (team <= 0 || self.team <= 0) continue;
    if (!objhome_get(brain, ent->id, &home)) qa_range_error("Bot objective lost its remembered home");
    if (team == self.team) { s->own_objective_home = home; s->has_own_objective_home = true; }
    else { s->enemy_objective_home = home; s->has_enemy_objective_home = true; }
  }
  if ((!s->objective_role || !s->objective_role[0]) && s->has_own_objective_home && s->has_enemy_objective_home) {
    free(s->objective_role);
    bool defend = qa_bot_random_chance(brain->config.rng, 25);
    s->objective_role = dups(defend ? "defend" : "attack");
    if (!s->objective_role) qa_range_error("Out of memory");
    emit_chat_once(brain, defend ? "ctf_on_defense" : "ctf_on_offense");
  }
  if (s->objective_role && !strcmp(s->objective_role, "attack") && s->has_enemy_objective_home && now > 0) {
    if (qa_bvec_distance(self.origin, s->enemy_objective_home) < OBJECTIVE_GUARD_RADIUS) emit_chat_once(brain, "ctf_camping_enemy_base");
  }
}
static bool objective_goal(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_self self,
                           const qa_bot_entity *target, double now, qa_bot_vec3 *out) {
  (void)now;
  qa_bot_brain_memory *s = &brain->state;
  if (!s->has_own_objective_home && !s->has_enemy_objective_home) return false;
  if (self.has_carrying_objective && self.carrying_objective && s->has_own_objective_home) {
    emit_chat_once(brain, "ctf_delivering_flag");
    s->goal_entity_id = -1;
    for (size_t i = 0; i < count; i++) {
      const qa_bot_entity *ent = &entities[i];
      if (ent->kind != QA_BOT_ENTITY_ITEM) continue;
      const qa_bot_item_rec *item = qa_bot_knowledge_get_item(brain->config.knowledge, ent->classname ? ent->classname : "");
      if (!item || !flag_includes(item, QA_ITEM_FLAG_OBJECTIVE)) continue;
      int64_t team = item->entry->has_team ? (int64_t)item->entry->team : ent->team;
      if (team != self.team) continue;
      if (qa_bvec_distance(ent->origin, s->own_objective_home) > OBJECTIVE_AWAY) continue;
      s->touch_goal = true;
      *out = ent->origin;
      return true;
    }
    if (qa_bvec_distance(self.origin, s->own_objective_home) < CARRIER_HOLD_RADIUS) s->hold_position = true;
    *out = s->own_objective_home;
    return true;
  }
  const qa_bot_entity *enemy_flag = NULL;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_ITEM) continue;
    const qa_bot_item_rec *item = qa_bot_knowledge_get_item(brain->config.knowledge, ent->classname ? ent->classname : "");
    if (!item || !flag_includes(item, QA_ITEM_FLAG_OBJECTIVE)) continue;
    int64_t team = item->entry->has_team ? (int64_t)item->entry->team : ent->team;
    if (team <= 0 || self.team <= 0) continue;
    qa_bot_vec3 home;
    bool has_home = objhome_get(brain, ent->id, &home);
    if (team == self.team) {
      if (has_home && qa_bvec_distance(ent->origin, home) > OBJECTIVE_AWAY) {
        emit_chat_once(brain, "ctf_returning_dropped_flag");
        s->goal_entity_id = (double)ent->id;
        *out = ent->origin;
        return true;
      }
      continue;
    }
    enemy_flag = ent;
  }
  const qa_bot_entity *our_carrier = NULL, *enemy_carrier = NULL;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_PLAYER || !(ent->has_carrying_objective && ent->carrying_objective) || ent->id == self.id || ent->dead) continue;
    if (ent->team != self.team) enemy_carrier = ent;
    else our_carrier = ent;
  }
  if (s->objective_role && !strcmp(s->objective_role, "defend")) {
    if (target) return false;
    if (enemy_carrier) {
      emit_chat_once(brain, "ctf_attacking_enemy_carrier");
      s->goal_entity_id = (double)enemy_carrier->id;
      s->goal_is_live = true;
      *out = enemy_carrier->origin;
      return true;
    }
    if (!s->has_own_objective_home) return false;
    if (qa_bvec_distance(self.origin, s->own_objective_home) < OBJECTIVE_GUARD_RADIUS) return false;
    s->goal_entity_id = -1;
    *out = s->own_objective_home;
    return true;
  }
  if (target && target->kind == QA_BOT_ENTITY_PLAYER && target->team > 0 && target->team != self.team)
    emit_chat_once(brain, "ctf_attacking_enemy_carrier");
  if (enemy_flag) {
    s->goal_entity_id = (double)enemy_flag->id;
    *out = enemy_flag->origin;
    return true;
  }
  if (our_carrier) {
    bool carrier_home = s->has_own_objective_home && qa_bvec_distance(our_carrier->origin, s->own_objective_home) < CARRIER_HOLD_RADIUS;
    if (carrier_home) {
      if (!enemy_carrier) return false;
      emit_chat_once(brain, "ctf_attacking_enemy_carrier");
      s->goal_entity_id = (double)enemy_carrier->id;
      s->goal_is_live = true;
      *out = enemy_carrier->origin;
      return true;
    }
    s->goal_entity_id = (double)our_carrier->id;
    s->goal_is_live = true;
    if (qa_bvec_distance(self.origin, our_carrier->origin) < ESCORT_DISTANCE) return false;
    *out = our_carrier->origin;
    return true;
  }
  if (!s->has_enemy_objective_home) return false;
  if (s->has_rest_point && qa_bvec_distance(s->rest_point, s->enemy_objective_home) < 1 && now < s->rest_until) return false;
  s->goal_entity_id = -1;
  *out = s->enemy_objective_home;
  return true;
}
static bool gate_to_shoot(qa_bot_brain *brain, qa_bot_world *world, const qa_bot_entity *entities, size_t count,
                          qa_bot_self self, bool has_bounds, qa_bot_vec3 mins, qa_bot_vec3 maxs, qa_bot_vec3 *out) {
  if (!has_bounds) return false;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_INTERACTABLE) continue;
    qa_bot_vec3 c = ent->center;
    if (c.x < mins.x - 8 || c.x > maxs.x + 8 || c.y < mins.y - 8 || c.y > maxs.y + 8 || c.z < mins.z - 8 || c.z > maxs.z + 8) continue;
    const char *how = qa_bot_knowledge_interaction_for(brain->config.knowledge, ent->classname ? ent->classname : "",
                                                       ent->spawnflags, ent->has_health, ent->has_targetname);
    if (!how || strcmp(how, QA_INTERACTION_SHOOT)) return false;
    if (qa_bvec_distance(self.eye, c) > GATE_SHOOT_RANGE) return false;
    qa_bot_trace trace = world->trace_line(world->ctx, self.eye, c);
    if (trace.fraction < 1 && trace.hit_id != ent->id) {
      qa_bot_trace again = world->trace_line(world->ctx, self.eye, c);
      (void)again;
      return false;
    }
    *out = c;
    return true;
  }
  return false;
}
static double pick_unstick_side(qa_bot_brain *brain, qa_bot_world *world, qa_bot_self self) {
  double first = qa_bot_random_chance(brain->config.rng, 50) ? 1 : -1;
  qa_angle_vectors av = qa_bot_angle_vectors(0, brain->state.aim.yaw, 0);
  int drop_sides = 0;
  double sides[2] = {first, -first};
  for (int i = 0; i < 2; i++) {
    double side = sides[i];
    qa_bot_vec3 at = {self.origin.x + av.right.x * side * UNSTICK_STEP, self.origin.y + av.right.y * side * UNSTICK_STEP, self.origin.z + 8};
    qa_bot_vec3 down_to = at; down_to.z -= UNSTICK_MAX_DROP;
    qa_bot_trace down = world->trace_box(world->ctx, at, v3(-16, -16, -24), v3(16, 16, 32), down_to);
    if (down.startsolid) continue;
    if (down.fraction < 1) return side;
    drop_sides++;
  }
  return drop_sides == 2 ? first : 0;
}
static void rest_static_goal(qa_bot_brain *brain, qa_bot_vec3 goal, double now) {
  qa_bot_brain_memory *s = &brain->state;
  if (s->goal_entity_id >= 0 || s->touch_goal || s->hold_position) return;
  s->rest_point = goal; s->has_rest_point = true;
  s->rest_until = now + POINT_REST_SECONDS;
}
static bool hazard_below(qa_bot_world *world, double x, double y, double z) {
  qa_bot_trace trace = world->trace_line(world->ctx, v3(x, y, z), v3(x, y, z - EDGE_DROP_CHECK));
  int64_t c = world->point_contents(world->ctx, v3(x, y, trace.endpos.z + 2));
  return c == QA_BOT_CONTENTS_LAVA || c == QA_BOT_CONTENTS_SLIME;
}
static bool hazard_under_segment(qa_bot_world *world, qa_bot_vec3 a, qa_bot_vec3 b) {
  double len = qa_bvec_distance(a, b);
  double steps_d = ceil(len / 64);
  int64_t steps = steps_d > 1 ? (int64_t)steps_d : 1;
  for (int64_t i = 1; i < steps; i++) {
    double f = (double)i / (double)steps;
    if (hazard_below(world, a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f)) return true;
  }
  return false;
}
static bool in_hazard(qa_bot_world *world, qa_bot_self self) {
  int64_t c1 = world->point_contents(world->ctx, self.origin);
  qa_bot_vec3 feet = self.origin; feet.z -= 20;
  int64_t c2 = world->point_contents(world->ctx, feet);
  return c1 == QA_BOT_CONTENTS_LAVA || c1 == QA_BOT_CONTENTS_SLIME || c2 == QA_BOT_CONTENTS_LAVA || c2 == QA_BOT_CONTENTS_SLIME;
}
static bool hazard_exit(qa_bot_world *world, qa_bot_self self, qa_bot_vec3 *out) {
  for (int i = 0; i < LAVA_EXIT_SAMPLES; i++) {
    double a = ((double)i / LAVA_EXIT_SAMPLES) * acos(-1.0) * 2;
    qa_bot_vec3 p = {self.origin.x + cos(a) * LAVA_EXIT_REACH, self.origin.y + sin(a) * LAVA_EXIT_REACH, self.origin.z};
    int64_t c = world->point_contents(world->ctx, p);
    if (c == QA_BOT_CONTENTS_LAVA || c == QA_BOT_CONTENTS_SLIME || c == QA_BOT_CONTENTS_SOLID) continue;
    qa_bot_vec3 below_point = p; below_point.z -= 24;
    int64_t below = world->point_contents(world->ctx, below_point);
    if (below == QA_BOT_CONTENTS_LAVA || below == QA_BOT_CONTENTS_SLIME) continue;
    *out = p;
    return true;
  }
  return false;
}
typedef struct { bool has; double x, y; } MoveDir;
static MoveDir move_direction(qa_bot_brain *brain, qa_bot_usercmd cmd) {
  double yaw = (brain->state.aim.yaw * acos(-1.0)) / 180;
  double fx = cos(yaw), fy = sin(yaw);
  double rx = sin(yaw), ry = -cos(yaw);
  double mx = fx * cmd.forwardmove + rx * cmd.sidemove;
  double my = fy * cmd.forwardmove + ry * cmd.sidemove;
  double ml = hypot(mx, my);
  if (ml < 1) return (MoveDir){false, 0, 0};
  return (MoveDir){true, mx / ml, my / ml};
}
typedef struct { bool has; double hazard_at; bool crossable; } GapAhead;
static GapAhead gap_ahead(qa_bot_brain *brain, qa_bot_world *world, qa_bot_self self, qa_bot_usercmd cmd,
                          bool has_steer, qa_bot_vec3 steer_at) {
  MoveDir dir = move_direction(brain, cmd);
  if (!dir.has) return (GapAhead){false, 0, false};
  double mx = dir.x, my = dir.y;
  double speed = hypot(self.velocity.x, self.velocity.y);
  double ahead = EDGE_LOOKAHEAD + speed * EDGE_STOP_SECONDS;
  if (has_steer) {
    double along = (steer_at.x - self.origin.x) * mx + (steer_at.y - self.origin.y) * my;
    if (along > EDGE_PROBE_STEP && along < ahead) ahead = along;
  }
  double hazard_at = -1;
  for (double d = EDGE_PROBE_STEP; d <= ahead + 0.01; d += EDGE_PROBE_STEP) {
    if (hazard_below(world, self.origin.x + mx * d, self.origin.y + my * d, self.origin.z)) { hazard_at = d; break; }
  }
  if (hazard_at < 0) return (GapAhead){false, 0, false};
  double floor_z = self.origin.z - 24;
  double cap = brain->settings->movement.walk_only
                   ? (brain->config.has_walk_speed ? brain->config.walk_speed : QA_BOT_WALK_SPEED)
                   : (brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED);
  double run_speed = fmax(speed, cap);
  double landing_start = -1, landing_z = 0;
  for (double d = hazard_at + EDGE_PROBE_STEP; d <= hazard_at + GAP_LANDING_SEARCH; d += EDGE_PROBE_STEP) {
    double x = self.origin.x + mx * d, y = self.origin.y + my * d;
    qa_bot_trace trace = world->trace_line(world->ctx, v3(x, y, self.origin.z), v3(x, y, self.origin.z - EDGE_DROP_CHECK));
    if (trace.startsolid) return (GapAhead){true, hazard_at, false};
    double land_z = trace.endpos.z;
    int64_t c = trace.fraction < 1 ? world->point_contents(world->ctx, v3(x, y, land_z + 2)) : QA_BOT_CONTENTS_LAVA;
    bool floor_here = trace.fraction < 1 && c != QA_BOT_CONTENTS_LAVA && c != QA_BOT_CONTENTS_SLIME;
    if (!floor_here) { landing_start = -1; continue; }
    double max_rise = brain->config.has_movement ? brain->config.movement.maximum_landing_rise : GAP_LANDING_RISE;
    if (land_z > floor_z + max_rise) return (GapAhead){true, hazard_at, false};
    if (landing_start < 0 || fabs(land_z - landing_z) > 8) { landing_start = d; landing_z = land_z; continue; }
    double drop = fmax(0, floor_z - landing_z);
    double jump_velocity = brain->config.has_movement ? brain->config.movement.jump_velocity : JUMP_VELOCITY;
    double gravity = brain->config.has_movement ? brain->config.movement.gravity : BRAIN_GRAVITY;
    double airtime = (jump_velocity + sqrt(jump_velocity * jump_velocity + 2 * gravity * drop)) / gravity;
    double reach = run_speed * airtime * GAP_REACH_MARGIN;
    return (GapAhead){true, hazard_at, landing_start + 16 <= reach};
  }
  return (GapAhead){true, hazard_at, false};
}
static bool jump_lands_in_hazard(qa_bot_brain *brain, qa_bot_world *world, qa_bot_self self, qa_bot_usercmd cmd) {
  double vx = self.velocity.x, vy = self.velocity.y;
  double speed = hypot(vx, vy);
  if (speed < 1) {
    MoveDir dir = move_direction(brain, cmd);
    if (!dir.has) return false;
    speed = brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED;
    vx = dir.x * speed; vy = dir.y * speed;
  }
  double reach = speed * (brain->config.has_movement ? brain->config.movement.jump_air_seconds : JUMP_AIR_SECONDS);
  double ux = vx / speed, uy = vy / speed;
  for (double d = EDGE_PROBE_STEP; d <= reach + 0.01; d += EDGE_PROBE_STEP)
    if (hazard_below(world, self.origin.x + ux * d, self.origin.y + uy * d, self.origin.z)) return true;
  return false;
}
static bool air_above(qa_bot_world *world, qa_bot_self self) {
  qa_bot_vec3 above = {self.eye.x, self.eye.y, self.eye.z + SURFACE_REACH};
  return world->point_contents(world->ctx, above) == QA_BOT_CONTENTS_EMPTY;
}
static bool interactable_goal(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_self self,
                              double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  if (now >= s->press_until) return false;
  if (s->has_own_objective_home || s->has_enemy_objective_home) return false;
  const qa_bot_entity *best = NULL;
  double best_range = INFINITY;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_INTERACTABLE) continue;
    const char *how = qa_bot_knowledge_interaction_for(brain->config.knowledge, ent->classname ? ent->classname : "",
                                                       ent->spawnflags, ent->has_health, ent->has_targetname);
    if (!how || strcmp(how, "push")) continue;
    if (ent->has_targetname) continue;
    double blocked;
    if (unreach_get(brain, ent->id, &blocked)) {
      if (blocked > now) continue;
      unreach_delete(brain, ent->id);
    }
    double range = qa_bvec_distance(self.origin, ent->center);
    if (range > INTERACT_RADIUS || range >= best_range) continue;
    best_range = range;
    best = ent;
  }
  if (!best) return false;
  if (best_range <= INTERACT_REACHED) { s->press_until = 0; return false; }
  s->goal_entity_id = (double)best->id;
  *out = best->center;
  return true;
}
static bool coop_regroup_goal(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_self self,
                              double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  if (!coop_game(brain)) return false;
  const qa_bot_entity *nearest = NULL;
  double best = INFINITY;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_PLAYER || ent->is_bot || ent->dead) continue;
    double d = qa_bvec_distance(self.origin, ent->origin);
    if (d < best) { best = d; nearest = ent; }
  }
  if (!nearest) { s->coop_regrouping = false; return false; }
  if (!s->coop_regrouping && now >= s->coop_regroup_at) {
    s->coop_regrouping = true;
    s->coop_regroup_until = now + COOP_REGROUP_GIVE_UP;
  }
  if (s->coop_regrouping && (best <= COOP_REGROUP_NEAR || now >= s->coop_regroup_until)) {
    s->coop_regrouping = false;
    s->coop_regroup_at = now + COOP_REGROUP_SECONDS;
  }
  if (!s->coop_regrouping) return false;
  s->goal_entity_id = -1;
  *out = nearest->origin;
  return true;
}
static bool coop_hunt_goal(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_self self,
                           double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  if (!coop_game(brain)) return false;
  const qa_bot_entity *human = NULL;
  double human_range = INFINITY;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_PLAYER || ent->is_bot || ent->dead) continue;
    double d = qa_bvec_distance(self.origin, ent->origin);
    if (d < human_range) { human_range = d; human = ent; }
  }
  const qa_bot_entity *best = NULL;
  double best_range = INFINITY;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_MONSTER || ent->dead) continue;
    if (friendly(brain, ent, self.team)) continue;
    double blocked;
    if (unreach_get(brain, ent->id, &blocked)) {
      if (blocked > now) continue;
      unreach_delete(brain, ent->id);
    }
    if (human && qa_bvec_distance(human->origin, ent->origin) > COOP_HUNT_RADIUS) continue;
    double d = qa_bvec_distance(self.origin, ent->origin);
    if (d < best_range) { best_range = d; best = ent; }
  }
  if (!best) return false;
  s->goal_entity_id = (double)best->id;
  s->goal_is_live = true;
  *out = best->origin;
  return true;
}
static const qa_bot_entity *best_item(qa_bot_brain *brain, qa_bot_world *world, const qa_bot_entity *entities,
                                      size_t count, double now, bool in_combat) {
  qa_bot_self self = world->get_self(world->ctx);
  bool defer_power = brain->settings->behaviors.defer_power_items_to_humans &&
                     brain->config.human_teammate_near && brain->config.human_teammate_near(brain->config.human_teammate_near_ctx);
  const qa_bot_entity *best = NULL;
  double best_score = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_ITEM) continue;
    double blocked_until;
    if (unreach_get(brain, ent->id, &blocked_until)) {
      if (blocked_until > now) continue;
      unreach_delete(brain, ent->id);
    }
    const qa_bot_item_rec *item = qa_bot_knowledge_get_item(brain->config.knowledge, ent->classname ? ent->classname : "");
    if (!item) continue;
    if (qa_bvec_distance(self.origin, ent->origin) > item->entry->sight_dist) continue;
    if (in_combat) {
      qa_bot_behavior_settings behavior = brain->settings->behaviors;
      if (item->is_weapon && !behavior.combat_grab_weapons) continue;
      if (item->is_health && self.health >= (brain->config.has_max_health ? brain->config.max_health : 100) * behavior.combat_min_health_pct / 100) continue;
      if (item->is_armor && self.armor >= (self.has_max_armor ? self.max_armor : 200) * behavior.combat_min_armor_pct / 100) continue;
      const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(brain->config.knowledge, (double)self.current_weapon);
      if (item->is_ammo && weapon && qa_bot_ammo_get(self.ammo, self.ammo_count, weapon->entry->ammo_name) >= weapon->entry->max_ammo * behavior.combat_min_ammo_pct / 100) continue;
    }
    if ((item->is_powerup || item->is_mega) && defer_power) continue;
    qa_bot_vec3 home;
    bool has_home = objhome_get(brain, ent->id, &home);
    size_t wcount;
    const qa_bot_weapon_rec *weapons = brain->config.knowledge->replaced ? brain->config.knowledge->replaced : brain->config.knowledge->weapons;
    wcount = brain->config.knowledge->replaced ? brain->config.knowledge->replaced_count : brain->config.knowledge->weapon_count;
    qa_bot_item_context ctx; memset(&ctx, 0, sizeof ctx);
    ctx.spawnflags = ent->spawnflags;
    ctx.health = self.health;
    ctx.max_health = brain->config.has_max_health ? brain->config.max_health : 100;
    ctx.armor = self.armor;
    ctx.items = self.items;
    ctx.ammo = self.ammo; ctx.ammo_count = self.ammo_count;
    ctx.weapon_stay = brain->config.game_mode.weapon_stay;
    ctx.allow_power_items = brain->settings->behaviors.allow_grab_power_items;
    ctx.weapons = weapons; ctx.weapon_count = wcount;
    ctx.team = self.team;
    ctx.item_team = item->entry->has_team ? (int64_t)item->entry->team : ent->team;
    ctx.objective_at_home = !has_home || qa_bvec_distance(ent->origin, home) <= OBJECTIVE_AWAY;
    double value = qa_bot_item_value(item, ctx);
    if (value <= 0) continue;
    double dist = fmax(64, qa_bvec_distance(self.origin, ent->origin));
    double score = (value * 1024) / dist;
    if (score > best_score) { best_score = score; best = ent; }
  }
  return best;
}
static double unreachable_rest(qa_bot_brain *brain) {
  return brain->state.goal_is_live ? UNREACHABLE_LIVE_SECONDS : UNREACHABLE_SECONDS;
}
static void abandon_goal(qa_bot_brain *brain) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_clear_path(&s->path_state);
  s->has_roam_point = false;
  s->roam_until = 0;
  s->has_goal_point = false;
  s->goal_entity_id = -1;
  if (s->has_explicit_goal) s->explicit_goal_failed = true;
}
static void reach_goal(qa_bot_brain *brain) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_clear_path(&s->path_state);
  s->stuck_trips = 0;
  s->press_until = 0;
  s->has_roam_point = false;
  s->roam_until = 0;
  if (s->has_explicit_goal) s->explicit_goal_done = true;
}
static bool blind_roam_goal(qa_bot_brain *brain, qa_bot_world *world, double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_self self = world->get_self(world->ctx);
  if (s->has_roam_point && now < s->roam_until && qa_bvec_distance(self.origin, s->roam_point) > 64) { *out = s->roam_point; return true; }
  double angle = ((double)qa_bot_random_index(brain->config.rng, 360) * acos(-1.0)) / 180;
  double reach = BLIND_ROAM_RADIUS / 2.0 + (double)qa_bot_random_index(brain->config.rng, BLIND_ROAM_RADIUS / 2);
  s->roam_point = v3(self.origin.x + cos(angle) * reach, self.origin.y + sin(angle) * reach, self.origin.z);
  s->has_roam_point = true;
  s->roam_until = now + BLIND_ROAM_SECONDS;
  *out = s->roam_point;
  return true;
}
static bool roam_goal(qa_bot_brain *brain, qa_bot_world *world, double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_navigation_iface *nav = world->nav ? world->nav(world->ctx) : NULL;
  if (!nav || nav->node_count(nav->ctx) == 0) return blind_roam_goal(brain, world, now, out);
  qa_bot_self self = world->get_self(world->ctx);
  if (s->has_roam_point && qa_bvec_distance(self.origin, s->roam_point) > 64 && s->path_state.path) { *out = s->roam_point; return true; }
  size_t count = nav->node_count(nav->ctx);
  qa_nav_graph_node *nodes = malloc(count * sizeof *nodes);
  if (!nodes) qa_range_error("Out of memory");
  count = nav->nodes(nav->ctx, nodes, count);
  bool found = false;
  for (int tries = 0; tries < 8; tries++) {
    int64_t index = qa_bot_random_index(brain->config.rng, (int64_t)count);
    if (index < 0 || index >= (int64_t)count) continue;
    if (qa_bvec_distance(self.origin, nodes[index].origin) > ROAM_RADIUS) continue;
    s->roam_point = nodes[index].origin;
    s->has_roam_point = true;
    s->path_state.planned_at = now - REPLAN_SECONDS;
    *out = s->roam_point;
    found = true;
    break;
  }
  free(nodes);
  if (found) return true;
  if (s->has_roam_point) { *out = s->roam_point; return true; }
  return false;
}
static bool select_goal(qa_bot_brain *brain, qa_bot_world *world, const qa_bot_entity *entities, size_t count,
                        const qa_bot_entity *target, double now, qa_bot_vec3 *out) {
  qa_bot_brain_memory *s = &brain->state;
  s->goal_entity_id = -1;
  s->goal_is_live = false;
  s->touch_goal = false;
  s->hold_position = false;
  if (s->has_explicit_goal && !s->explicit_goal_done && !s->explicit_goal_failed) {
    if (s->explicit_goal.is_entity) {
      const qa_bot_entity *ent = NULL;
      for (size_t i = 0; i < count; i++) if ((double)entities[i].id == (double)s->explicit_goal.entity_id) ent = &entities[i];
      if (!ent) s->explicit_goal_failed = true;
      else {
        s->explicit_goal.point = ent->origin;
        s->goal_point = ent->origin; s->has_goal_point = true;
        *out = ent->origin;
        return true;
      }
    } else {
      s->goal_point = s->explicit_goal.point; s->has_goal_point = true;
      *out = s->goal_point;
      return true;
    }
  }
  qa_bot_self self = world->get_self(world->ctx);
  qa_bot_vec3 goal;
  if (objective_goal(brain, entities, count, self, target, now, &goal)) {
    s->goal_point = goal; s->has_goal_point = true;
    *out = goal;
    return true;
  }
  if (target) {
    qa_bot_awareness *aw = awareness_get(brain, target->id);
    s->goal_point = aw ? aw->last_known_origin : target->origin;
    s->has_goal_point = true;
    if (brain->settings->behaviors.allow_grab_items_in_combat) {
      const qa_bot_entity *item = best_item(brain, world, entities, count, now, true);
      if (item && qa_bvec_distance(self.origin, item->origin) < brain->settings->behaviors.combat_max_item_dist) {
        s->goal_entity_id = (double)item->id;
        *out = item->origin;
        return true;
      }
    }
    *out = s->goal_point;
    return true;
  }
  if (interactable_goal(brain, entities, count, self, now, &goal)) { s->goal_point = goal; s->has_goal_point = true; *out = goal; return true; }
  if (coop_regroup_goal(brain, entities, count, self, now, &goal)) { s->goal_point = goal; s->has_goal_point = true; *out = goal; return true; }
  if (coop_hunt_goal(brain, entities, count, self, now, &goal)) { s->goal_point = goal; s->has_goal_point = true; *out = goal; return true; }
  if (brain->settings->behaviors.allow_grab_items) {
    const qa_bot_entity *item = best_item(brain, world, entities, count, now, false);
    if (item) {
      s->goal_entity_id = (double)item->id;
      s->goal_point = item->origin; s->has_goal_point = true;
      *out = item->origin;
      return true;
    }
  }
  return roam_goal(brain, world, now, out);
}
typedef struct { qa_bot_world *world; qa_bot_id_time *memo; size_t memo_count, memo_cap; } CapsCtx;
static bool caps_avoid(void *ctx, qa_nav_graph_node node) {
  CapsCtx *c = ctx;
  for (size_t i = 0; i < c->memo_count; i++)
    if (c->memo[i].entity == node.index) return c->memo[i].time != 0;
  qa_bot_vec3 point = {node.origin.x, node.origin.y, node.origin.z + 8};
  int64_t contents = c->world->point_contents(c->world->ctx, point);
  bool bad = contents == QA_BOT_CONTENTS_LAVA || contents == QA_BOT_CONTENTS_SLIME;
  if (c->memo_count == c->memo_cap) {
    c->memo_cap = c->memo_cap ? c->memo_cap * 2 : 32;
    c->memo = realloc(c->memo, c->memo_cap * sizeof *c->memo);
    if (!c->memo) qa_range_error("Out of memory");
  }
  c->memo[c->memo_count++] = (qa_bot_id_time){node.index, bad ? 1 : 0};
  return bad;
}
typedef struct { qa_bot_brain *brain; qa_bot_world *world; } VisibleCtx;
static bool visible_fn(void *ctx, qa_bot_vec3 from, qa_bot_vec3 to) {
  VisibleCtx *c = ctx;
  qa_bot_vec3 mins = c->brain->config.has_movement ? c->brain->config.movement.body_mins : v3(-16, -16, -24 + 18);
  qa_bot_vec3 maxs = c->brain->config.has_movement ? c->brain->config.movement.body_maxs : v3(16, 16, 32);
  qa_bot_trace t = c->world->trace_box(c->world->ctx, from, mins, maxs, to);
  if (t.fraction < 1 || t.startsolid) return false;
  return !hazard_under_segment(c->world, from, to);
}
static qa_bot_transport_step transport_thunk(void *ctx, qa_nav_graph_link link, qa_bot_vec3 origin) {
  qa_bot_world *world = ctx;
  qa_bot_transport_step none; memset(&none, 0, sizeof none);
  qa_bot_navigation_iface *nav = world->nav ? world->nav(world->ctx) : NULL;
  if (!nav || !nav->has_transport) return none;
  return nav->transport(nav->ctx, link, origin);
}
static void ensure_path(qa_bot_brain *brain, qa_bot_world *world, qa_bot_vec3 goal, double now) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_self self = world->get_self(world->ctx);
  qa_bot_navigation_iface *nav = world->nav ? world->nav(world->ctx) : NULL;
  if (!nav) { qa_bot_clear_path(&s->path_state); return; }
  const qa_nav_path *current = s->path_state.path;
  qa_nav_graph_link train_link; bool has_train = false;
  if (current) {
    qa_nav_graph_link cur, prev;
    bool has_cur = s->path_state.index >= 0 && s->path_state.index < (int64_t)current->link_count && current->has_link[s->path_state.index];
    bool has_prev = s->path_state.index > 0 && s->path_state.index - 1 < (int64_t)current->link_count && current->has_link[s->path_state.index - 1];
    if (has_cur) cur = current->links[s->path_state.index];
    if (has_prev) prev = current->links[s->path_state.index - 1];
    if (has_cur && cur.type == QA_NAV_LINK_TRAIN) { train_link = cur; has_train = true; }
    else if (has_prev && prev.type == QA_NAV_LINK_TRAIN) { train_link = prev; has_train = true; }
  }
  if (has_train) {
    qa_bot_transport_step step = transport_thunk(world, train_link, self.origin);
    if (step.has && (step.kind == QA_TRANSPORT_RIDE || step.kind == QA_TRANSPORT_WAIT ||
                     (step.kind == QA_TRANSPORT_MOVE && step.exit_stage)))
      return;
  }
  bool stale = !current || !nav->path_valid(nav->ctx, current) || now - s->path_state.planned_at > REPLAN_SECONDS;
  if (!stale) {
    if (current->point_count == 0) return;
    qa_bot_vec3 end = current->points[current->point_count - 1];
    if (qa_bvec_distance(end, goal) < 96) return;
  }
  CapsCtx caps_ctx; memset(&caps_ctx, 0, sizeof caps_ctx); caps_ctx.world = world;
  qa_nav_traverse_caps caps = qa_default_traverse_caps();
  caps.jump = !brain->settings->movement.walk_only || brain->settings->movement.allow_jumping_in_combat;
  caps.avoid_ctx = &caps_ctx; caps.avoid = caps_avoid;
  VisibleCtx visible_ctx = {brain, world};
  qa_nav_plan_options options; memset(&options, 0, sizeof options);
  options.has_caps = true; options.caps = caps;
  options.visible_ctx = &visible_ctx; options.visible = visible_fn;
  options.has_start_above = true;
  options.start_above = brain->config.has_movement ? brain->config.movement.start_above : QA_PLAN_START_ABOVE;
  qa_nav_path path;
  bool planned = nav->plan_path(nav->ctx, self.origin, goal, &options, &path);
  free(caps_ctx.memo);
  if (s->has_owned_path) { qa_nav_path_destroy(&s->owned_path); s->has_owned_path = false; }
  if (planned) {
    s->owned_path = path;
    s->has_owned_path = true;
    qa_bot_set_path(&s->path_state, &s->owned_path, self.origin, now);
  } else {
    qa_bot_set_path(&s->path_state, NULL, self.origin, now);
    s->path_state.planned_at = now;
  }
}
static qa_bot_vec3 aim_point_for(qa_bot_brain *brain, const qa_bot_entity *target, qa_bot_vec3 from, double weapon_number) {
  const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(brain->config.knowledge, weapon_number);
  const char *point = weapon ? weapon->entry->aim_point : "center";
  if (point && !strcmp(point, "head")) return target->head;
  if (point && !strcmp(point, "feet")) return target->feet;
  if (point && !strcmp(point, "best")) {
    if (weapon && weapon_flag_includes(weapon, "explosive") && fabs(target->origin.z - from.z) < 32) return target->feet;
    size_t count = 0;
    (void)qa_bot_knowledge_skill_name(brain->config.knowledge, 0, &count);
    int64_t rank = -1;
    for (size_t i = 0; i < count; i++) {
      const char *name = qa_bot_knowledge_skill_name(brain->config.knowledge, i, NULL);
      if (name && !strcmp(name, brain->settings->skill)) rank = (int64_t)i;
    }
    if (rank >= (int64_t)count - 2) return target->head;
  }
  return target->center;
}
static bool select_weapon(qa_bot_brain *brain, qa_bot_self self, const qa_bot_entity *target, double *out) {
  size_t wcount;
  const qa_bot_weapon_rec *weapons = brain->config.knowledge->replaced ? brain->config.knowledge->replaced : brain->config.knowledge->weapons;
  wcount = brain->config.knowledge->replaced ? brain->config.knowledge->replaced_count : brain->config.knowledge->weapon_count;
  qa_bot_weapon_context ctx; memset(&ctx, 0, sizeof ctx);
  ctx.items = self.items; ctx.ammo = self.ammo; ctx.ammo_count = self.ammo_count;
  ctx.range = qa_bvec_distance(self.origin, target->origin);
  ctx.height_delta = target->origin.z - self.origin.z;
  ctx.in_water = self.water_level >= 2;
  ctx.has_protection = self.has_protection;
  ctx.target_in_water = target->water_level >= 2;
  ctx.allow_melee = brain->settings->behaviors.allow_melee;
  qa_bot_weapon_pick pick;
  if (!qa_choose_weapon(weapons, wcount, ctx, &pick)) return false;
  *out = pick.number;
  return true;
}
static bool trigger(qa_bot_brain *brain, double weapon_number, bool aimed, double now) {
  qa_bot_brain_memory *s = &brain->state;
  const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(brain->config.knowledge, weapon_number);
  if (!weapon || !weapon->entry->trigger_hold_and_release) {
    s->trigger_weapon = 0; s->trigger_held_since = -1; s->trigger_ready_at = 0;
    return aimed;
  }
  if (s->trigger_weapon != weapon_number) { s->trigger_weapon = weapon_number; s->trigger_held_since = -1; s->trigger_ready_at = 0; }
  if (s->trigger_held_since >= 0) {
    if (aimed && now - s->trigger_held_since < weapon->entry->trigger_hold) return true;
    s->trigger_held_since = -1; s->trigger_ready_at = now + weapon->entry->trigger_cooldown;
    return false;
  }
  if (!aimed || now < s->trigger_ready_at) return false;
  s->trigger_held_since = now;
  return true;
}
static bool should_check_six(qa_bot_brain *brain, double now) {
  qa_bot_brain_memory *s = &brain->state;
  if (!brain->settings->behaviors.allow_check_six) return false;
  if (now < s->check_six_until) return true;
  if (now < s->check_six_next_at) return false;
  s->check_six_next_at = now + qa_bot_random_range(brain->config.rng, 3, 8);
  if (!qa_bot_random_chance(brain->config.rng, 35)) return false;
  s->check_six_until = now + 0.5;
  return true;
}
static bool wants_use(qa_bot_brain *brain, const qa_bot_entity *entities, size_t count, qa_bot_vec3 origin) {
  for (size_t i = 0; i < count; i++) {
    const qa_bot_entity *ent = &entities[i];
    if (ent->kind != QA_BOT_ENTITY_INTERACTABLE) continue;
    if (qa_bvec_distance(origin, ent->center) > 96) continue;
    const char *how = qa_bot_knowledge_interaction_for(brain->config.knowledge, ent->classname ? ent->classname : "",
                                                       ent->spawnflags, ent->has_health, ent->has_targetname);
    if (how && (!strcmp(how, "use") || !strcmp(how, "push"))) return true;
  }
  return false;
}
qa_bot_usercmd qa_bot_brain_think(qa_bot_brain *brain, qa_bot_world *world) {
  qa_bot_brain_memory *s = &brain->state;
  qa_bot_self self = world->get_self(world->ctx);
  double now = world->time(world->ctx);
  double dt = world->frame_time(world->ctx);
  qa_bot_usercmd cmd = qa_empty_usercmd();
  if (!s->spawned_once) { s->spawned_once = true; qa_bot_brain_emit_chat(brain, "connected"); }
  if (!s->level_started) { s->level_started = true; qa_bot_brain_emit_chat(brain, "match_start"); }
  s->aim.pitch = self.view_angles.x;
  s->aim.yaw = self.view_angles.y;
  if (self.dead) {
    s->trigger_weapon = 0; s->trigger_held_since = -1; s->trigger_ready_at = 0;
    cmd.view_angles = v3(s->aim.pitch, s->aim.yaw, 0);
    if (s->dead_since < 0) {
      s->dead_since = now;
      s->respawn_wait = qa_bot_random_range(brain->config.rng, brain->settings->behaviors.min_respawn_time,
                                            brain->settings->behaviors.max_respawn_time);
      s->respawn_press = false;
    }
    if (now - s->dead_since >= s->respawn_wait) {
      s->respawn_press = !s->respawn_press;
      if (s->respawn_press) cmd.buttons |= QA_BOT_BUTTON_ATTACK;
    }
    qa_bot_clear_path(&s->path_state);
    s->target_id = -1;
    s->awareness_count = 0;
    s->last_cmd = cmd;
    return cmd;
  }
  s->dead_since = -1;
  size_t entity_count = 0, sound_count = 0;
  const qa_bot_entity *entities = world->entities(world->ctx, &entity_count);
  const qa_bot_sound *sounds = world->hearing(world->ctx, &sound_count);
  update_senses(brain, world, self, entities, entity_count, sounds, sound_count, dt, now);
  update_objectives(brain, entities, entity_count, self, now);
  const qa_bot_entity *target = select_target(brain, entities, entity_count, self.team, self.origin);
  s->target_id = target ? (double)target->id : -1;
  bool wedged = update_wedge(brain, self.origin, now, target != NULL);
  qa_bot_vec3 goal_value;
  bool has_goal = select_goal(brain, world, entities, entity_count, target, now, &goal_value);
  bool has_move_target = false;
  qa_bot_vec3 move_target = {0, 0, 0};
  bool riding_lift = false;
  bool has_hazard_face = false;
  qa_bot_vec3 hazard_face = {0, 0, 0};
  bool in_hazard_now = false;
  if (has_goal && s->hold_position) {
    qa_bot_clear_path(&s->path_state);
    s->wedge_origin = self.origin; s->has_wedge_origin = true;
    s->wedge_since = now;
  } else if (has_goal) {
    ensure_path(brain, world, goal_value, now);
    qa_bot_navigation_iface *nav = world->nav ? world->nav(world->ctx) : NULL;
    if (!s->path_state.path && nav && s->goal_entity_id >= 0) {
      unreach_set(brain, (int64_t)s->goal_entity_id, now + unreachable_rest(brain));
      abandon_goal(brain);
      s->press_until = now + INTERACT_SECONDS;
    }
    qa_bot_follow_input follow_input; memset(&follow_input, 0, sizeof follow_input);
    follow_input.transport_ctx = world; follow_input.transport = transport_thunk;
    follow_input.origin = self.origin;
    follow_input.pitch = s->aim.pitch; follow_input.yaw = s->aim.yaw;
    follow_input.on_ground = self.on_ground;
    follow_input.has_water_level = true; follow_input.water_level = (double)self.water_level;
    if (self.has_air_seconds) { follow_input.has_air_seconds = true; follow_input.air_seconds = self.air_seconds; }
    if (self.water_level >= 3) { follow_input.has_air_above = true; follow_input.air_above = air_above(world, self); }
    follow_input.has_velocity = true; follow_input.velocity = self.velocity;
    follow_input.now = now; follow_input.stuck_time = STUCK_SECONDS;
    if (brain->config.has_run_speed) { follow_input.has_run_speed = true; follow_input.run_speed = brain->config.run_speed; }
    if (brain->config.has_walk_speed) { follow_input.has_walk_speed = true; follow_input.walk_speed = brain->config.walk_speed; }
    qa_bot_move_output follow = qa_follow_path(&s->path_state, follow_input, brain->settings->movement);
    if (follow.status == QA_BOT_PATH_STUCK) {
      s->stuck_trips++;
      s->unstick_until = now + UNSTICK_SECONDS;
      s->unstick_side = pick_unstick_side(brain, world, self);
      if (s->stuck_trips >= STUCK_GIVE_UP) {
        if (s->goal_entity_id >= 0) unreach_set(brain, (int64_t)s->goal_entity_id, now + unreachable_rest(brain));
        abandon_goal(brain);
        s->press_until = now + INTERACT_SECONDS;
        s->stuck_trips = 0;
      } else {
        qa_bot_clear_path(&s->path_state);
      }
    } else if (follow.status == QA_BOT_PATH_ARRIVED) {
      if (s->touch_goal && qa_bvec_distance(self.origin, goal_value) >= TOUCH_RADIUS) {
        qa_bot_steer direct = qa_steer_direct(self.origin, s->aim.yaw, goal_value, brain->settings->movement.walk_only,
            brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED,
            brain->config.has_walk_speed ? brain->config.walk_speed : QA_BOT_WALK_SPEED);
        cmd.forwardmove = direct.forwardmove;
        cmd.sidemove = direct.sidemove;
        move_target = goal_value; has_move_target = true;
      } else {
        rest_static_goal(brain, goal_value, now);
        reach_goal(brain);
      }
    } else if (follow.status == QA_BOT_PATH_MOVING) {
      s->stuck_trips = 0;
      cmd.forwardmove = follow.forwardmove;
      cmd.sidemove = follow.sidemove;
      cmd.upmove = follow.upmove;
      if (follow.jump) cmd.buttons |= QA_BOT_BUTTON_JUMP;
      riding_lift = follow.has_riding && follow.riding;
      if (follow.has_target) { move_target = follow.target; has_move_target = true; }
      qa_bot_vec3 gate;
      bool has_gate = follow.has_link && follow.link.has_entity_bounds &&
                      gate_to_shoot(brain, world, entities, entity_count, self, true, follow.link.entity_mins, follow.link.entity_maxs, &gate);
      s->has_gate_shoot_at = has_gate;
      if (has_gate) s->gate_shoot_at = gate;
    } else if (follow.status == QA_BOT_PATH_NO_PATH) {
      double reach = s->touch_goal ? TOUCH_RADIUS : 48;
      if (qa_bvec_distance(self.origin, goal_value) < reach) {
        rest_static_goal(brain, goal_value, now);
        reach_goal(brain);
      } else {
        qa_bot_steer direct = qa_steer_direct(self.origin, s->aim.yaw, goal_value, brain->settings->movement.walk_only,
            brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED,
            brain->config.has_walk_speed ? brain->config.walk_speed : QA_BOT_WALK_SPEED);
        cmd.forwardmove = direct.forwardmove;
        cmd.sidemove = direct.sidemove;
        move_target = goal_value; has_move_target = true;
      }
    }
  }
  if (in_hazard(world, self)) {
    s->hazard_frames++;
    in_hazard_now = true;
    qa_bot_clear_path(&s->path_state);
    qa_bot_vec3 exit_point; bool has_exit;
    if (s->has_last_safe_origin) { exit_point = s->last_safe_origin; has_exit = true; }
    else has_exit = hazard_exit(world, self, &exit_point);
    if (has_exit) {
      s->aim.yaw = (atan2(exit_point.y - self.origin.y, exit_point.x - self.origin.x) * 180) / acos(-1.0);
      s->aim.pitch = 0;
      hazard_face = exit_point; has_hazard_face = true;
      qa_bot_steer direct = qa_steer_direct(self.origin, s->aim.yaw, exit_point, false,
          brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED,
          brain->config.has_walk_speed ? brain->config.walk_speed : QA_BOT_WALK_SPEED);
      cmd.forwardmove = direct.forwardmove;
      cmd.sidemove = direct.sidemove;
      move_target = exit_point; has_move_target = true;
    }
    cmd.upmove = self.water_level >= 3 ? (brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED) : 0;
    cmd.buttons &= ~QA_BOT_BUTTON_JUMP;
    s->wedge_origin = self.origin; s->has_wedge_origin = true;
    s->wedge_since = now;
  } else if (self.on_ground && self.water_level == 0) {
    s->last_safe_origin = self.origin; s->has_last_safe_origin = true;
  }
  if (target && qa_roll_combat_jump(&s->path_state, brain->settings->movement, brain->config.rng, now, self.on_ground))
    cmd.buttons |= QA_BOT_BUTTON_JUMP;
  if (wedged) {
    if (s->unstick_until <= now) {
      s->unstick_until = now + UNSTICK_SECONDS;
      s->unstick_side = pick_unstick_side(brain, world, self);
      qa_bot_clear_path(&s->path_state);
    }
    if (now - s->wedge_since >= WEDGED_GIVE_UP_SECONDS) {
      if (s->goal_entity_id >= 0) unreach_set(brain, (int64_t)s->goal_entity_id, now + unreachable_rest(brain));
      abandon_goal(brain);
      s->press_until = now + INTERACT_SECONDS;
      s->wedge_origin = self.origin; s->has_wedge_origin = true;
      s->wedge_since = now;
    }
  }
  if (now < s->unstick_until) {
    double speed = brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED;
    cmd.sidemove = s->unstick_side * speed;
    cmd.forwardmove *= 0.5;
    if (self.on_ground) cmd.buttons |= QA_BOT_BUTTON_JUMP;
  }
  s->last_guard_refused = false;
  if (!in_hazard_now && self.on_ground) {
    bool planned_jump = false;
    const qa_nav_path *path = s->path_state.path;
    if (path && s->path_state.index >= 0 && s->path_state.index < (int64_t)path->link_count && path->has_link[s->path_state.index]) {
      qa_nav_link_type type = path->links[s->path_state.index].type;
      planned_jump = type == QA_NAV_LINK_LONG_JUMP || type == QA_NAV_LINK_MANUAL_LONG_JUMP;
    }
    bool gap_jump_pressed = false;
    if (!planned_jump && (cmd.forwardmove != 0 || cmd.sidemove != 0)) {
      GapAhead gap = gap_ahead(brain, world, self, cmd, has_move_target, move_target);
      if (gap.has && gap.crossable && now < s->unstick_until) gap.crossable = false;
      if (gap.has && !gap.crossable && now < s->unstick_until) {
        cmd.sidemove = -cmd.sidemove;
        s->unstick_side = -s->unstick_side;
        gap = gap_ahead(brain, world, self, cmd, has_move_target, move_target);
        if (gap.has && !gap.crossable) {
          cmd.sidemove = 0;
          cmd.forwardmove = -(brain->config.has_run_speed ? brain->config.run_speed : QA_BOT_RUN_SPEED);
          cmd.buttons &= ~QA_BOT_BUTTON_JUMP;
          gap = gap_ahead(brain, world, self, cmd, has_move_target, move_target);
        }
      }
      if (gap.has && gap.crossable) {
        if (gap.hazard_at <= GAP_JUMP_TRIGGER) {
          cmd.buttons |= QA_BOT_BUTTON_JUMP;
          s->gap_jumps++;
          gap_jump_pressed = true;
        }
      } else if (gap.has) {
        s->guard_refusals++;
        s->last_guard_refused = true;
        cmd.forwardmove = 0;
        cmd.sidemove = 0;
        cmd.buttons &= ~QA_BOT_BUTTON_JUMP;
        if (!s->path_state.path && s->has_roam_point && s->goal_entity_id < 0 && !s->touch_goal && !s->hold_position)
          abandon_goal(brain);
      }
    }
    if ((cmd.buttons & QA_BOT_BUTTON_JUMP) != 0 && !planned_jump && !gap_jump_pressed && jump_lands_in_hazard(brain, world, self, cmd)) {
      s->guard_refusals++;
      cmd.buttons &= ~QA_BOT_BUTTON_JUMP;
    }
  }
  if (s->hold_position) {
    cmd.forwardmove = 0;
    cmd.sidemove = 0;
    cmd.upmove = 0;
  }
  if (self.has_on_lift && self.on_lift && !riding_lift && cmd.forwardmove == 0 && cmd.sidemove == 0)
    cmd.forwardmove = brain->config.has_walk_speed ? brain->config.walk_speed : QA_BOT_WALK_SPEED;
  /* aim */
  bool has_aim_at = false;
  qa_bot_vec3 aim_at = {0, 0, 0};
  if (has_hazard_face) {
    (void)hazard_face;
  } else if (target) {
    qa_bot_awareness *aw = awareness_get(brain, target->id);
    qa_bot_vec3 point = aim_point_for(brain, target, self.origin, (double)self.current_weapon);
    qa_bot_vec3 lead = qa_aim_lead_point(point, target->velocity, brain->settings->aiming);
    const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(brain->config.knowledge, (double)self.current_weapon);
    if (brain->settings->aiming.lead_targets && weapon && weapon->entry->speed > 0)
      lead = qa_bvec_ma(lead, qa_bvec_distance(self.eye, point) / weapon->entry->speed, target->velocity);
    aim_at = qa_bvec_sub(lead, self.eye);
    has_aim_at = true;
    if (aw && aw->last_seen < now) aim_at = qa_bvec_sub(aw->last_known_origin, self.eye);
  } else if (s->has_gate_shoot_at) {
    aim_at = qa_bvec_sub(s->gate_shoot_at, self.eye);
    has_aim_at = true;
  } else if (should_check_six(brain, now)) {
    aim_at = v3(cos(((s->aim.yaw + 180) * acos(-1.0)) / 180), sin(((s->aim.yaw + 180) * acos(-1.0)) / 180), 0);
    has_aim_at = true;
  } else if (has_move_target) {
    aim_at = qa_bvec_sub(move_target, self.origin);
    aim_at.z = 0;
    has_aim_at = true;
  }
  if (has_aim_at) (void)qa_aim_step(&s->aim, aim_at, brain->settings->aiming, dt, now);
  cmd.view_angles = v3(s->aim.pitch, qa_bot_angle_mod(s->aim.yaw), 0);
  if (s->has_gate_shoot_at && !target && now - s->gate_fired_at >= GATE_SHOT_SECONDS) {
    if (qa_bot_angle_between(s->aim.pitch, s->aim.yaw, qa_bvec_sub(s->gate_shoot_at, self.eye)) <= GATE_AIM_DEGREES) {
      cmd.buttons |= QA_BOT_BUTTON_ATTACK;
      s->gate_fired_at = now;
    }
  }
  if (target) {
    double pick;
    bool has_pick = select_weapon(brain, self, target, &pick);
    if (has_pick && pick != (double)self.current_weapon && pick != s->last_weapon_number) {
      double impulse = brain->config.weapon_impulse ? brain->config.weapon_impulse(brain->config.weapon_impulse_ctx, pick) : 0;
      if (impulse > 0) {
        cmd.impulse = (int64_t)impulse;
        s->last_weapon_number = pick;
      } else if (brain->config.on_weapon_select) {
        brain->config.on_weapon_select(brain->config.on_weapon_select_ctx, pick);
        s->last_weapon_number = pick;
      }
    } else if (has_pick && pick == (double)self.current_weapon) {
      s->last_weapon_number = 0;
    }
    qa_bot_awareness *aw = awareness_get(brain, target->id);
    bool aimed = aw && qa_bot_can_fire(aw) && has_aim_at &&
                 qa_aim_error(&s->aim, aim_at) < weapon_cone(brain, (double)self.current_weapon) / 2;
    if (trigger(brain, (double)self.current_weapon, aimed, now)) cmd.buttons |= QA_BOT_BUTTON_ATTACK;
  } else {
    s->trigger_held_since = -1;
  }
  if (wants_use(brain, entities, entity_count, self.origin)) cmd.buttons |= QA_BOT_BUTTON_USE;
  s->last_cmd = cmd;
  return cmd;
}
