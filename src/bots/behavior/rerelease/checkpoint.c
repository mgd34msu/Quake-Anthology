/* Port of src/bots/behavior/rerelease/checkpoint.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/checkpoint.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>

static char *dups(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p;
}
static qa_save_reader field(const qa_save_reader *parent, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(parent, name, &out)) qa_range_error("Out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *parent, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(parent, index, &out)) qa_range_error("Out of memory");
  return out;
}
static double fin_field(const qa_save_reader *parent, const char *name) {
  qa_save_reader r = field(parent, name);
  double v = qa_save_reader_finite(&r);
  qa_save_reader_destroy(&r);
  return v;
}
static bool bool_field(const qa_save_reader *parent, const char *name) {
  qa_save_reader r = field(parent, name);
  bool v = qa_save_reader_boolean(&r);
  qa_save_reader_destroy(&r);
  return v;
}
static char *string_field(const qa_save_reader *parent, const char *name) {
  qa_save_reader r = field(parent, name);
  char *v = dups(qa_save_reader_string(&r));
  qa_save_reader_destroy(&r);
  if (!v) qa_range_error("Out of memory");
  return v;
}
static int64_t int_min_field(const qa_save_reader *parent, const char *name, double minimum) {
  qa_save_reader r = field(parent, name);
  double v = qa_save_reader_integer_min(&r, minimum);
  qa_save_reader_destroy(&r);
  return (int64_t)v;
}
static int64_t int_field(const qa_save_reader *parent, const char *name) {
  qa_save_reader r = field(parent, name);
  double v = qa_save_reader_integer(&r);
  qa_save_reader_destroy(&r);
  return (int64_t)v;
}
static qa_bot_vec3 read_vector(const qa_save_reader *parent) {
  double x = fin_field(parent, "x"), y = fin_field(parent, "y"), z = fin_field(parent, "z");
  return (qa_bot_vec3){x, y, z};
}
static qa_bot_vec3 vector_field(const qa_save_reader *parent, const char *name) {
  qa_save_reader r = field(parent, name);
  qa_bot_vec3 v = read_vector(&r);
  qa_save_reader_destroy(&r);
  return v;
}
static bool nullable_vector_field(const qa_save_reader *parent, const char *name, qa_bot_vec3 *out) {
  qa_save_reader r = field(parent, name);
  if (qa_save_reader_is_null(&r)) { qa_save_reader_destroy(&r); return false; }
  *out = read_vector(&r);
  qa_save_reader_destroy(&r);
  return true;
}
static bool read_mode(const qa_save_reader *parent, qa_bot_game_mode *out) {
  memset(out, 0, sizeof *out);
  out->game_type_owned = string_field(parent, "gameType");
  out->game_type = out->game_type_owned;
  out->weapon_stay = bool_field(parent, "weaponStay");
  qa_save_reader teams = field(parent, "hasTeams");
  bool has_teams_present = !qa_save_reader_is_undefined(&teams);
  bool has_teams = has_teams_present ? qa_save_reader_boolean(&teams) : false;
  qa_save_reader_destroy(&teams);
  qa_save_reader damage = field(parent, "teamDamage");
  bool has_damage_present = !qa_save_reader_is_undefined(&damage);
  bool team_damage = has_damage_present ? qa_save_reader_boolean(&damage) : false;
  qa_save_reader_destroy(&damage);
  out->has_has_teams = has_teams_present; out->has_teams = has_teams;
  out->has_team_damage = has_damage_present; out->team_damage = team_damage;
  return true;
}
static qa_nav_link_type read_link_type(const qa_save_reader *parent) {
  double value = qa_save_reader_integer_min(parent, 0);
  if (value >= 0 && value <= 10) return (qa_nav_link_type)(int64_t)value;
  qa_save_reader_fail(parent, "Invalid navigation link type");
}
static bool read_path(const qa_save_reader *parent, qa_nav_path *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader nodes_r = field(parent, "nodes");
  size_t node_count = qa_save_reader_list_count(&nodes_r);
  if (node_count) {
    out->nodes = malloc(node_count * sizeof *out->nodes);
    if (!out->nodes) { qa_save_reader_destroy(&nodes_r); return false; }
    for (size_t i = 0; i < node_count; i++) {
      qa_save_reader it = item(&nodes_r, i);
      out->nodes[i] = (int64_t)qa_save_reader_integer_min(&it, 0);
      qa_save_reader_destroy(&it);
    }
  }
  out->node_count = node_count;
  qa_save_reader_destroy(&nodes_r);
  qa_save_reader points_r = field(parent, "points");
  size_t point_count = qa_save_reader_list_count(&points_r);
  if (point_count) {
    out->points = malloc(point_count * sizeof *out->points);
    if (!out->points) { qa_save_reader_destroy(&points_r); return false; }
    for (size_t i = 0; i < point_count; i++) {
      qa_save_reader it = item(&points_r, i);
      out->points[i] = read_vector(&it);
      qa_save_reader_destroy(&it);
    }
  }
  out->point_count = point_count;
  qa_save_reader_destroy(&points_r);
  qa_save_reader links_r = field(parent, "links");
  size_t link_count = qa_save_reader_list_count(&links_r);
  if (link_count) {
    out->links = calloc(link_count, sizeof *out->links);
    out->has_link = calloc(link_count, sizeof *out->has_link);
    if (!out->links || !out->has_link) { qa_save_reader_destroy(&links_r); return false; }
    for (size_t i = 0; i < link_count; i++) {
      qa_save_reader link = item(&links_r, i);
      if (!qa_save_reader_is_null(&link)) {
        qa_nav_graph_link value; memset(&value, 0, sizeof value);
        value.from = int_min_field(&link, "from", 0);
        value.to = int_min_field(&link, "to", 0);
        qa_save_reader type_r = field(&link, "type");
        value.type = read_link_type(&type_r);
        qa_save_reader_destroy(&type_r);
        qa_save_reader traversal = field(&link, "traversal");
        if (!qa_save_reader_is_null(&traversal)) {
          value.has_traversal = true;
          value.traversal.funnel = vector_field(&traversal, "funnel");
          value.traversal.start = vector_field(&traversal, "start");
          value.traversal.end = vector_field(&traversal, "end");
        }
        qa_save_reader_destroy(&traversal);
        qa_save_reader bounds = field(&link, "entityBounds");
        if (!qa_save_reader_is_null(&bounds)) {
          value.has_entity_bounds = true;
          value.entity_mins = vector_field(&bounds, "mins");
          value.entity_maxs = vector_field(&bounds, "maxs");
        }
        qa_save_reader_destroy(&bounds);
        out->links[i] = value;
        out->has_link[i] = true;
      }
      qa_save_reader_destroy(&link);
    }
  }
  out->link_count = link_count;
  qa_save_reader_destroy(&links_r);
  out->cost = fin_field(parent, "cost");
  out->generation = int_min_field(parent, "generation", 0);
  char *digest = string_field(parent, "mapDigest");
  out->map_digest = digest; /* retained: qa_nav_path borrows its digest */
  return true;
}
static bool read_memory(const qa_save_reader *r, qa_bot_brain_checkpoint_memory *m) {
  memset(m, 0, sizeof *m);
  m->trigger_weapon = fin_field(r, "triggerWeapon");
  m->trigger_held_since = fin_field(r, "triggerHeldSince");
  m->trigger_ready_at = fin_field(r, "triggerReadyAt");
  m->target_id = fin_field(r, "targetId");
  m->has_goal_point = nullable_vector_field(r, "goalPoint", &m->goal_point);
  m->goal_entity_id = fin_field(r, "goalEntityId");
  m->stuck_trips = fin_field(r, "stuckTrips");
  m->goal_is_live = bool_field(r, "goalIsLive");
  m->unstick_until = fin_field(r, "unstickUntil");
  m->press_until = fin_field(r, "pressUntil");
  m->unstick_side = fin_field(r, "unstickSide");
  m->explicit_goal_done = bool_field(r, "explicitGoalDone");
  m->explicit_goal_failed = bool_field(r, "explicitGoalFailed");
  m->has_wedge_origin = nullable_vector_field(r, "wedgeOrigin", &m->wedge_origin);
  m->wedge_since = fin_field(r, "wedgeSince");
  m->has_last_safe_origin = nullable_vector_field(r, "lastSafeOrigin", &m->last_safe_origin);
  m->has_rest_point = nullable_vector_field(r, "restPoint", &m->rest_point);
  m->rest_until = fin_field(r, "restUntil");
  m->guard_refusals = fin_field(r, "guardRefusals");
  m->last_guard_refused = bool_field(r, "lastGuardRefused");
  m->gap_jumps = fin_field(r, "gapJumps");
  m->hazard_frames = fin_field(r, "hazardFrames");
  m->has_own_objective_home = nullable_vector_field(r, "ownObjectiveHome", &m->own_objective_home);
  m->has_enemy_objective_home = nullable_vector_field(r, "enemyObjectiveHome", &m->enemy_objective_home);
  m->objective_role = string_field(r, "objectiveRole");
  m->touch_goal = bool_field(r, "touchGoal");
  m->hold_position = bool_field(r, "holdPosition");
  m->has_gate_shoot_at = nullable_vector_field(r, "gateShootAt", &m->gate_shoot_at);
  m->gate_fired_at = fin_field(r, "gateFiredAt");
  m->coop_regrouping = bool_field(r, "coopRegrouping");
  m->coop_regroup_at = fin_field(r, "coopRegroupAt");
  m->coop_regroup_until = fin_field(r, "coopRegroupUntil");
  m->level_started = bool_field(r, "levelStarted");
  m->check_six_until = fin_field(r, "checkSixUntil");
  m->check_six_next_at = fin_field(r, "checkSixNextAt");
  m->has_roam_point = nullable_vector_field(r, "roamPoint", &m->roam_point);
  m->roam_until = fin_field(r, "roamUntil");
  m->spawned_once = bool_field(r, "spawnedOnce");
  m->last_weapon_number = fin_field(r, "lastWeaponNumber");
  m->dead_since = fin_field(r, "deadSince");
  m->respawn_wait = fin_field(r, "respawnWait");
  m->respawn_press = bool_field(r, "respawnPress");
  qa_save_reader aim = field(r, "aim");
  m->aim.pitch = fin_field(&aim, "pitch");
  m->aim.yaw = fin_field(&aim, "yaw");
  m->aim.pitch_velocity = fin_field(&aim, "pitchVelocity");
  m->aim.yaw_velocity = fin_field(&aim, "yawVelocity");
  m->aim.modifier_until = fin_field(&aim, "modifierUntil");
  qa_save_reader_destroy(&aim);
  qa_save_reader follow = field(r, "pathState");
  qa_save_reader path_r = field(&follow, "path");
  if (qa_save_reader_is_null(&path_r)) m->has_path = false;
  else {
    if (!read_path(&path_r, &m->path)) { qa_save_reader_destroy(&path_r); qa_save_reader_destroy(&follow); return false; }
    m->has_path = true;
  }
  qa_save_reader_destroy(&path_r);
  m->path_index = int_min_field(&follow, "index", 0);
  m->stuck_origin = vector_field(&follow, "stuckOrigin");
  m->stuck_since = fin_field(&follow, "stuckSince");
  m->stuck_count = int_min_field(&follow, "stuckCount", 0);
  m->lift_wait_since = fin_field(&follow, "liftWaitSince");
  m->lift_wait_z = fin_field(&follow, "liftWaitZ");
  m->jump_ready_at = fin_field(&follow, "jumpReadyAt");
  m->planned_at = fin_field(&follow, "plannedAt");
  qa_save_reader_destroy(&follow);
  qa_save_reader aw_r = field(r, "awareness");
  size_t aw_count = qa_save_reader_list_count(&aw_r);
  if (aw_count) {
    m->awareness = calloc(aw_count, sizeof *m->awareness);
    if (!m->awareness) { qa_save_reader_destroy(&aw_r); return false; }
    for (size_t i = 0; i < aw_count; i++) {
      qa_save_reader entry = item(&aw_r, i);
      m->awareness[i].entity = int_field(&entry, "entity");
      qa_save_reader value = field(&entry, "value");
      qa_bot_awareness *aw = &m->awareness[i].value;
      aw->id = int_field(&value, "id");
      aw->sight = fin_field(&value, "sight");
      aw->weapon = fin_field(&value, "weapon");
      aw->last_contact = fin_field(&value, "lastContact");
      aw->last_seen = fin_field(&value, "lastSeen");
      aw->last_heard = fin_field(&value, "lastHeard");
      aw->last_known_origin = vector_field(&value, "lastKnownOrigin");
      qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&entry);
    }
  }
  m->awareness_count = aw_count;
  qa_save_reader_destroy(&aw_r);
  qa_save_reader un_r = field(r, "unreachableUntil");
  size_t un_count = qa_save_reader_list_count(&un_r);
  if (un_count) {
    m->unreachable_until = calloc(un_count, sizeof *m->unreachable_until);
    if (!m->unreachable_until) { qa_save_reader_destroy(&un_r); return false; }
    for (size_t i = 0; i < un_count; i++) {
      qa_save_reader entry = item(&un_r, i);
      m->unreachable_until[i].entity = int_field(&entry, "entity");
      m->unreachable_until[i].time = fin_field(&entry, "time");
      qa_save_reader_destroy(&entry);
    }
  }
  m->unreachable_count = un_count;
  qa_save_reader_destroy(&un_r);
  qa_save_reader oh_r = field(r, "objectiveHome");
  size_t oh_count = qa_save_reader_list_count(&oh_r);
  if (oh_count) {
    m->objective_home = calloc(oh_count, sizeof *m->objective_home);
    if (!m->objective_home) { qa_save_reader_destroy(&oh_r); return false; }
    for (size_t i = 0; i < oh_count; i++) {
      qa_save_reader entry = item(&oh_r, i);
      m->objective_home[i].entity = int_field(&entry, "entity");
      m->objective_home[i].origin = vector_field(&entry, "origin");
      qa_save_reader_destroy(&entry);
    }
  }
  m->objective_home_count = oh_count;
  qa_save_reader_destroy(&oh_r);
  qa_save_reader said_r = field(r, "saidThisLevel");
  size_t said_count = qa_save_reader_list_count(&said_r);
  if (said_count) {
    m->said_this_level = calloc(said_count, sizeof *m->said_this_level);
    if (!m->said_this_level) { qa_save_reader_destroy(&said_r); return false; }
    for (size_t i = 0; i < said_count; i++) {
      qa_save_reader entry = item(&said_r, i);
      m->said_this_level[i] = dups(qa_save_reader_string(&entry));
      qa_save_reader_destroy(&entry);
      if (!m->said_this_level[i]) { qa_save_reader_destroy(&said_r); m->said_count = said_count; return false; }
    }
  }
  m->said_count = said_count;
  qa_save_reader_destroy(&said_r);
  qa_save_reader goal = field(r, "explicitGoal");
  if (qa_save_reader_is_null(&goal)) m->has_explicit_goal = false;
  else {
    m->has_explicit_goal = true;
    qa_save_reader owner = field(&goal, "owner");
    if (qa_save_reader_is_undefined(&owner)) m->explicit_goal.objective_owner = false;
    else {
      const char *choices[] = {"external", "objective"};
      m->explicit_goal.objective_owner = !strcmp(qa_save_reader_choice_string(&owner, choices, 2), "objective");
    }
    qa_save_reader_destroy(&owner);
    qa_save_reader kind = field(&goal, "kind");
    const char *kinds[] = {"point", "entity"};
    m->explicit_goal.is_entity = !strcmp(qa_save_reader_choice_string(&kind, kinds, 2), "entity");
    qa_save_reader_destroy(&kind);
    m->explicit_goal.point = vector_field(&goal, "point");
    m->explicit_goal.entity_id = int_field(&goal, "entityId");
  }
  qa_save_reader_destroy(&goal);
  qa_save_reader cmd = field(r, "lastCmd");
  m->last_cmd.forwardmove = fin_field(&cmd, "forwardmove");
  m->last_cmd.sidemove = fin_field(&cmd, "sidemove");
  m->last_cmd.upmove = fin_field(&cmd, "upmove");
  m->last_cmd.buttons = int_min_field(&cmd, "buttons", 0);
  m->last_cmd.impulse = int_min_field(&cmd, "impulse", 0);
  m->last_cmd.view_angles = vector_field(&cmd, "viewAngles");
  qa_save_reader_destroy(&cmd);
  return true;
}
bool qa_read_rerelease_behavior(qa_rerelease_behavior_checkpoint *out, const qa_save_value *value) {
  memset(out, 0, sizeof *out);
  qa_save_reader r;
  if (!qa_save_reader_init(&r, value, "bot.rerelease")) return false;
  qa_save_reader b = field(&r, "brain");
  qa_save_reader version_r = field(&b, "version");
  out->brain.version = (int64_t)qa_save_reader_literal_number(&version_r, 1);
  qa_save_reader_destroy(&version_r);
  out->brain.skill = string_field(&b, "skill");
  qa_save_reader mode_r = field(&b, "gameMode");
  (void)read_mode(&mode_r, &out->brain.game_mode);
  qa_save_reader_destroy(&mode_r);
  qa_save_reader memory_r = field(&b, "memory");
  bool memory_ok = read_memory(&memory_r, &out->brain.memory);
  qa_save_reader_destroy(&memory_r);
  qa_save_reader_destroy(&b);
  if (!memory_ok) { qa_save_reader_destroy(&r); qa_rerelease_behavior_checkpoint_destroy(out); return false; }
  qa_save_reader top_version = field(&r, "version");
  out->version = (int64_t)qa_save_reader_literal_number(&top_version, 1);
  qa_save_reader_destroy(&top_version);
  qa_save_reader source_r = field(&r, "source");
  const char *sources[] = {"q1-rerelease", "q2-rerelease"};
  (void)qa_rerelease_source_from_name(qa_save_reader_choice_string(&source_r, sources, 2), &out->source);
  qa_save_reader_destroy(&source_r);
  out->definition = string_field(&r, "definition");
  out->rng = int_field(&r, "rng");
  qa_save_reader chats_r = field(&r, "pendingChats");
  size_t chat_count = qa_save_reader_list_count(&chats_r);
  bool ok = true;
  if (chat_count) {
    out->pending_chats = calloc(chat_count, sizeof *out->pending_chats);
    if (!out->pending_chats) ok = false;
    for (size_t i = 0; ok && i < chat_count; i++) {
      qa_save_reader entry = item(&chats_r, i);
      out->pending_chats[i].time = fin_field(&entry, "time");
      qa_save_reader event = field(&entry, "event");
      out->pending_chats[i].locstring = string_field(&event, "locstring");
      out->pending_chats[i].type = string_field(&event, "type");
      out->pending_chats[i].delay_ms = fin_field(&event, "delayMs");
      out->pending_chats[i].team_only = bool_field(&event, "teamOnly");
      qa_save_reader_destroy(&event);
      qa_save_reader_destroy(&entry);
    }
  }
  out->pending_count = ok ? chat_count : 0;
  qa_save_reader_destroy(&chats_r);
  qa_save_reader_destroy(&r);
  if (!ok) { qa_rerelease_behavior_checkpoint_destroy(out); return false; }
  return true;
}
