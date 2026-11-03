/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OFFSET(field, offset)                                                                      \
  _Static_assert(offsetof(qa_bot_state_source, field) == (offset), #field " offset")
_Static_assert(sizeof(qa_bot_goal_state_bots_a) == 56, "bot_goal_t");
_Static_assert(sizeof(qa_bot_activate_goal_source) == 244, "bot_activategoal_t");
_Static_assert(offsetof(qa_bot_activate_goal_source, areas) == 104, "areas");
_Static_assert(offsetof(qa_bot_activate_goal_source, num_areas) == 232, "num_areas");
_Static_assert(sizeof(qa_bot_player_state_source) == 468, "playerState_t");
_Static_assert(offsetof(qa_bot_player_state_source, grapple_point) == 92, "grapple");
_Static_assert(offsetof(qa_bot_player_state_source, stats) == 184, "stats");
_Static_assert(offsetof(qa_bot_player_state_source, entity_event_sequence) == 464, "sequence");
_Static_assert(sizeof(qa_bot_user_command_source) == 24, "usercmd_t");
_Static_assert(sizeof(qa_bot_state_source) == QA_BOT_STATE_SOURCE_BYTES, "bot_state_t");
_Static_assert(QA_BOT_STATE_SOURCE_BYTES == 9088, "source bytes");
OFFSET(cur_ps, 16);
OFFSET(last_eflags, 484);
OFFSET(last_ucmd, 488);
OFFSET(entity_event_time, 512);
OFFSET(characterfile, 4608);
OFFSET(skill, 4752);
OFFSET(team, 4756);
OFFSET(think_time, 4904);
OFFSET(origin, 4908);
OFFSET(velocity, 4920);
OFFSET(presence_type, 4932);
OFFSET(eye, 4936);
OFFSET(area_num, 4948);
OFFSET(inventory, 4952);
OFFSET(tfl, 5976);
OFFSET(chat_to, 6052);
OFFSET(walker, 6056);
OFFSET(enter_game_time, 6064);
OFFSET(predict_obstacles_time, 6212);
OFFSET(predict_obstacles_goal_area_num, 6216);
OFFSET(aim_target, 6220);
OFFSET(kamikaze_body, 6256);
OFFSET(prox_mines, 6260);
OFFSET(num_prox_mines, 6516);
OFFSET(character, 6520);
OFFSET(enemy, 6540);
OFFSET(last_enemy_origin, 6548);
OFFSET(weapon_num, 6560);
OFFSET(viewangles, 6564);
OFFSET(ltg_type, 6600);
OFFSET(ordered, 6612);
OFFSET(own_decision_time, 6620);
OFFSET(team_goal, 6624);
OFFSET(alt_route_goal, 6680);
OFFSET(reached_alt_route_goal_time, 6736);
OFFSET(team_task_preference, 6752);
OFFSET(last_goal_team_goal, 6768);
OFFSET(lead_teammate, 6824);
OFFSET(lead_team_goal, 6828);
OFFSET(lead_time, 6884);
OFFSET(team_leader, 6900);
OFFSET(ask_team_leader_time, 6932);
OFFSET(num_teammates, 6948);
OFFSET(ctf_strategy, 6976);
OFFSET(subteam, 6980);
OFFSET(formation_dist, 7012);
OFFSET(formation_teammate, 7016);
OFFSET(formation_angle, 7032);
OFFSET(formation_dir, 7036);
OFFSET(formation_goal, 7060);
OFFSET(activate_goal_heap, 7120);
OFFSET(patrol_flags, 9084);

/* JS ToInt32, the DataView.setInt32 conversion. */
static int32_t to_int32(double value) {
  if (!isfinite(value))
    return 0;
  double t = fmod(trunc(value), 4294967296.0);
  if (t < 0)
    t += 4294967296.0;
  return (int32_t)(uint32_t)t;
}
static qa_save_value *sv(qa_save_value *value) {
  if (value == NULL)
    qa_range_error("Out of memory building bot state checkpoint");
  return value;
}
static void obj_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child))
    qa_range_error("Out of memory building bot state checkpoint");
}
static void arr_push(qa_save_value *array, qa_save_value *child) {
  if (!qa_save_value_push(array, child))
    qa_range_error("Out of memory building bot state checkpoint");
}
static qa_save_value *id_value(int64_t id) {
  return sv(id < 0 ? qa_save_value_null() : qa_save_value_number((double)id));
}
static void field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out))
    qa_range_error("Out of memory reading bot state checkpoint");
}
static void item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out))
    qa_range_error("Out of memory reading bot state checkpoint");
}

static const char *const NODE_NAMES[] = {NULL,       "intermission", "observer",     "respawn",
                                         "stand",    "seek-nbg",     "seek-ltg",     "battle-fight",
                                         "battle-chase", "battle-retreat", "battle-nbg",
                                         "seek-activate-entity"};
const char *qa_ai_node_name_bots_a(qa_ai_node_bots_a node) { return NODE_NAMES[node]; }
static const char *const STAGE_NAMES[] = {
    "allocated",   "character", "settings",  "goal-state",     "item-weights", "weapon-state",
    "weapon-weights", "chat-state", "chat-file", "chat-gender",  "published",    "move-state",
    "walker",      "counted",   "scheduled", "interbred",      "session",      "aas"};
static const char *const SETUP_KINDS[] = {"empty", "setting-up", "failed", "complete"};

void qa_bot_goal_state_copy_from(qa_bot_goal_state_bots_a *d, qa_bot_goal s) {
  d->origin = qa_bot_fv3(s.origin);
  d->area = to_int32((double)s.area);
  d->mins = qa_bot_fv3(s.mins);
  d->maxs = qa_bot_fv3(s.maxs);
  d->entity = to_int32((double)s.entity);
  d->number = to_int32((double)s.number);
  d->flags = to_int32((double)s.flags);
  d->item_info = to_int32((double)s.item_info);
}
void qa_bot_goal_state_clear(qa_bot_goal_state_bots_a *g) { memset(g, 0, sizeof *g); }
qa_bot_goal qa_bot_goal_state_get(const qa_bot_goal_state_bots_a *g) {
  qa_bot_goal out;
  memset(&out, 0, sizeof out);
  out.origin = qa_bot_v3(g->origin);
  out.area = g->area;
  out.mins = qa_bot_v3(g->mins);
  out.maxs = qa_bot_v3(g->maxs);
  out.entity = g->entity;
  out.number = g->number;
  out.flags = g->flags;
  out.item_info = g->item_info;
  return out;
}

void qa_bot_activate_goal_init(qa_bot_activate_goal_bots_a *g) {
  memset(g, 0, sizeof *g);
  g->src = &g->own;
}
void qa_bot_activate_goal_copy_from(qa_bot_activate_goal_bots_a *d,
                                    const qa_bot_activate_goal_bots_a *s) {
  int32_t cell = d->src->next_cell;
  *d->src = *s->src;
  d->src->next_cell = cell;
  d->next = s->next;
}
void qa_bot_activate_goal_clear(qa_bot_activate_goal_bots_a *g) {
  int32_t cell = g->src->next_cell;
  memset(g->src, 0, sizeof *g->src);
  g->src->next_cell = cell;
  g->next = NULL;
}

bool qa_bot_state_bots_a_init(qa_bot_state_bots_a *s, qa_q3_base_product product,
                              const qa_q3_game_memory_allocation *allocation) {
  memset(s, 0, sizeof *s);
  s->product = product;
  s->setup.kind = QA_BOT_SETUP_EMPTY;
  if (allocation != NULL) {
    if (allocation->length < QA_BOT_STATE_SOURCE_BYTES)
      qa_range_error("bot_state_t requires 9088 bytes");
    s->has_source_allocation = true;
    s->source_allocation = *allocation;
    s->src = (qa_bot_state_source *)(void *)allocation->bytes;
  } else {
    s->owned = calloc(1, sizeof *s->owned);
    if (s->owned == NULL)
      return false;
    s->src = s->owned;
  }
  for (int i = 0; i < QA_MAX_ACTIVATESTACK; i++)
    s->activate_goal_heap[i].src = &s->src->activate_goal_heap[i];
  return true;
}
void qa_bot_state_bots_a_destroy(qa_bot_state_bots_a *s) {
  free(s->owned);
  s->owned = NULL;
  s->src = NULL;
}

int32_t qa_bot_state_presence_type_checked(const qa_bot_state_bots_a *s) {
  int32_t v = s->src->presence_type;
  if (v != 0 && v != 1 && v != 2 && v != 4)
    qa_range_error("Unsupported bot presence type %d", v);
  return v;
}
int32_t qa_bot_state_pm_type_checked(const qa_bot_state_bots_a *s) {
  int32_t v = s->src->cur_ps.pm_type;
  if (v != QA_Q3_PM_NORMAL && v != QA_Q3_PM_NOCLIP && v != QA_Q3_PM_SPECTATOR &&
      v != QA_Q3_PM_DEAD && v != QA_Q3_PM_FREEZE && v != QA_Q3_PM_INTERMISSION &&
      v != QA_Q3_PM_SPINTERMISSION)
    qa_range_error("Unsupported bot player pmType %d", v);
  return v;
}
static bool command_weapon(int32_t v) {
  return v == QA_Q3_WP_NONE || v == QA_Q3_WP_GAUNTLET || v == QA_Q3_WP_MACHINEGUN ||
         v == QA_Q3_WP_SHOTGUN || v == QA_Q3_WP_GRENADE_LAUNCHER || v == QA_Q3_WP_ROCKET_LAUNCHER ||
         v == QA_Q3_WP_LIGHTNING || v == QA_Q3_WP_RAILGUN || v == QA_Q3_WP_PLASMAGUN ||
         v == QA_Q3_WP_BFG || v == QA_Q3_WP_GRAPPLING_HOOK || v == QA_Q3_WP_NAILGUN ||
         v == QA_Q3_WP_PROX_LAUNCHER || v == QA_Q3_WP_CHAINGUN;
}
int32_t qa_bot_state_weapon_checked(const qa_bot_state_bots_a *s) {
  int32_t v = s->src->cur_ps.weapon;
  if (!command_weapon(v))
    qa_range_error("Unsupported bot player weapon %d", v);
  return v;
}
int32_t qa_bot_state_weapon_state_checked(const qa_bot_state_bots_a *s) {
  int32_t v = s->src->cur_ps.weapon_state;
  if (v != QA_Q3_WEAPON_READY && v != QA_Q3_WEAPON_RAISING && v != QA_Q3_WEAPON_DROPPING &&
      v != QA_Q3_WEAPON_FIRING)
    qa_range_error("Unsupported bot player weaponState %d", v);
  return v;
}
int32_t qa_bot_state_command_weapon_checked(const qa_bot_state_bots_a *s) {
  int32_t v = s->src->last_ucmd.weapon;
  if (!command_weapon(v))
    qa_range_error("Unsupported bot command weapon %d", v);
  return v;
}

const char *qa_bot_state_text(const qa_bot_state_bots_a *s, const char *cell, const char *name) {
  const char *base = (const char *)s->src;
  if (cell < base || cell >= base + QA_BOT_STATE_SOURCE_BYTES ||
      memchr(cell, 0, (size_t)(base + QA_BOT_STATE_SOURCE_BYTES - cell)) == NULL)
    qa_range_error("Bot %s string reads beyond source storage", name);
  return cell;
}
/* strncpy copies until NUL and pads, with no extra terminator at count. */
static void source_strncpy(char *dst, const char *src, size_t count) {
  size_t length = strnlen(src, count);
  memcpy(dst, src, length);
  memset(dst + length, 0, count - length);
}
void qa_bot_state_set_text(char *cell, size_t size, const char *value) {
  source_strncpy(cell, value, size - 1);
  cell[size - 1] = 0;
}
/* Q_CleanStr leaves bytes after its new terminator untouched. */
static void clean_source_string(qa_bot_state_bots_a *s, char *text) {
  const char *end = (const char *)s->src + QA_BOT_STATE_SOURCE_BYTES;
  const char *read = text;
  char *write = text;
  for (;;) {
    if (read >= end)
      qa_range_error("Bot name reads beyond source storage");
    unsigned char byte = (unsigned char)*read;
    if (byte == 0)
      break;
    unsigned char next = read + 1 < end ? (unsigned char)read[1] : 0;
    if (byte == 94 && read + 1 < end && next != 0 && next != 94)
      read += 2;
    else {
      if (byte >= 32 && byte <= 126)
        *write++ = (char)byte;
      read++;
    }
  }
  *write = 0;
}
void qa_bot_state_copy_team_leader_client_name(qa_bot_state_bots_a *s, const char *raw) {
  source_strncpy(s->src->team_leader, raw, 31);
  s->src->team_leader[31] = 0;
  clean_source_string(s, s->src->team_leader);
}
void qa_bot_state_copy_team_leader_with_overflow(qa_bot_state_bots_a *s, const char *name) {
  uint8_t *bytes = (uint8_t *)s->src;
  source_strncpy((char *)bytes + 6900, name, 32);
  bytes[6932] = 0;
}
void qa_bot_state_copy_team_leader(qa_bot_state_bots_a *s, const char *name) {
  source_strncpy(s->src->team_leader, name, 31);
  s->src->team_leader[31] = 0;
}
void qa_bot_state_clear_team_leader(qa_bot_state_bots_a *s) { s->src->team_leader[0] = 0; }
void qa_bot_state_copy_subteam(qa_bot_state_bots_a *s, const char *name) {
  source_strncpy(s->src->subteam, name, 32);
  s->src->subteam[31] = 0;
}
void qa_bot_state_clear_subteam(qa_bot_state_bots_a *s) { s->src->subteam[0] = 0; }

void qa_bot_state_reset_decision_state(qa_bot_state_bots_a *s) {
  uint8_t *b = (uint8_t *)s->src;
  s->has_scripted_order = false;
  /* BotResetState's memset/restore leaves inuse/client/entitynum, cur_ps, settings,
   * entergame_time and character/ms/gs/cs/ws unchanged, including their raw bits. */
  memset(b + 4, 0, 4);
  memset(b + 484, 0, 4608 - 484);
  memset(b + 4900, 0, 6064 - 4900);
  memset(b + 6068, 0, 6520 - 6068);
  memset(b + 6540, 0, QA_BOT_STATE_SOURCE_BYTES - 6540);
  s->ai_node = QA_AI_NODE_NONE;
  s->activate_stack = NULL;
  for (int i = 0; i < QA_MAX_ACTIVATESTACK; i++)
    s->activate_goal_heap[i].next = NULL;
  s->checkpoints = NULL;
  s->patrol_points = NULL;
  s->current_patrol_point = NULL;
}
void qa_bot_state_clear(qa_bot_state_bots_a *s) {
  memset(s->src, 0, QA_BOT_STATE_SOURCE_BYTES);
  qa_bot_state_reset_decision_state(s);
  memset(&s->setup, 0, sizeof s->setup);
  s->setup.kind = QA_BOT_SETUP_EMPTY;
}

static int64_t activation_id(const qa_bot_state_bots_a *s, const qa_bot_activate_goal_bots_a *g) {
  if (g == NULL)
    return -1;
  for (int i = 0; i < QA_MAX_ACTIVATESTACK; i++)
    if (&s->activate_goal_heap[i] == g)
      return i;
  qa_range_error("Bot activation pointer escapes its heap");
}
qa_save_value *qa_bot_state_capture_save_state(const qa_bot_state_bots_a *s,
                                               qa_bot_waypoint_id_fn waypoint_id, void *ctx) {
  qa_save_value *out = sv(qa_save_value_object());
  obj_set(out, "bytes", sv(qa_save_value_bytes((const uint8_t *)s->src, QA_BOT_STATE_SOURCE_BYTES)));
  const char *node = qa_ai_node_name_bots_a(s->ai_node);
  obj_set(out, "aiNode", sv(node == NULL ? qa_save_value_null() : qa_save_value_string(node)));
  qa_save_value *setup = sv(qa_save_value_object());
  obj_set(setup, "kind", sv(qa_save_value_string(SETUP_KINDS[s->setup.kind])));
  if (s->setup.kind == QA_BOT_SETUP_SETTING_UP || s->setup.kind == QA_BOT_SETUP_FAILED)
    obj_set(setup, "stage", sv(qa_save_value_string(STAGE_NAMES[s->setup.stage])));
  if (s->setup.kind == QA_BOT_SETUP_FAILED && s->setup.stage != QA_BOT_SETUP_AAS &&
      s->setup.stage != QA_BOT_SETUP_CHARACTER)
    obj_set(setup, "errorCode", sv(qa_save_value_number((double)s->setup.error_code)));
  obj_set(out, "setup", setup);
  if (!s->has_scripted_order)
    obj_set(out, "scriptedOrder", sv(qa_save_value_null()));
  else {
    static const char *const PROGRESS[] = {"in-progress", "success", "error"};
    qa_save_value *order = sv(qa_save_value_object()), *body = sv(qa_save_value_object());
    obj_set(order, "progress", sv(qa_save_value_string(PROGRESS[s->scripted_order.progress])));
    if (s->scripted_order.order.kind == QA_BOT_ORDER_POINT) {
      qa_save_value *point = sv(qa_save_value_object());
      obj_set(point, "x", sv(qa_save_value_number(s->scripted_order.order.point.x)));
      obj_set(point, "y", sv(qa_save_value_number(s->scripted_order.order.point.y)));
      obj_set(point, "z", sv(qa_save_value_number(s->scripted_order.order.point.z)));
      obj_set(body, "kind", sv(qa_save_value_string("point")));
      obj_set(body, "point", point);
    } else {
      qa_save_value *entity = sv(qa_save_value_object());
      obj_set(entity, "number", sv(qa_save_value_number((double)s->scripted_order.order.entity.number)));
      obj_set(entity, "generation",
              sv(qa_save_value_number((double)s->scripted_order.order.entity.generation)));
      obj_set(body, "kind", sv(qa_save_value_string("follow")));
      obj_set(body, "entity", entity);
    }
    obj_set(order, "order", body);
    obj_set(out, "scriptedOrder", order);
  }
  obj_set(out, "activateStack", id_value(activation_id(s, s->activate_stack)));
  qa_save_value *next = sv(qa_save_value_array());
  for (int i = 0; i < QA_MAX_ACTIVATESTACK; i++)
    arr_push(next, id_value(activation_id(s, s->activate_goal_heap[i].next)));
  obj_set(out, "activationNext", next);
  obj_set(out, "checkpoints", id_value(waypoint_id(ctx, s->checkpoints)));
  obj_set(out, "patrolPoints", id_value(waypoint_id(ctx, s->patrol_points)));
  obj_set(out, "currentPatrolPoint", id_value(waypoint_id(ctx, s->current_patrol_point)));
  return out;
}

static int64_t nullable_integer(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  int64_t v = qa_save_reader_is_null(&cell) ? -1 : (int64_t)qa_save_reader_integer_min(&cell, 0);
  qa_save_reader_destroy(&cell);
  return v;
}
static int64_t choice(const qa_save_reader *r, const char *const *names, size_t count) {
  const char *text = qa_save_reader_choice_string(r, names, count);
  for (size_t i = 0; i < count; i++)
    if (names[i] != NULL && strcmp(names[i], text) == 0)
      return (int64_t)i;
  qa_save_reader_fail(r, "invalid choice");
}
static qa_bot_activate_goal_bots_a *activation_at(qa_bot_state_bots_a *s, const qa_save_reader *cell) {
  if (qa_save_reader_is_null(cell))
    return NULL;
  double id = qa_save_reader_integer_min(cell, 0);
  if (id >= QA_MAX_ACTIVATESTACK)
    qa_save_reader_fail(cell, "activation pointer outside heap");
  return &s->activate_goal_heap[(int)id];
}
void qa_bot_state_restore_save_state(qa_bot_state_bots_a *s, const qa_save_value *value,
                                     qa_bot_waypoint_at_fn waypoint, void *waypoint_ctx,
                                     qa_bot_remap_observation_fn remap, void *remap_ctx) {
  qa_save_reader reader, cell, inner;
  if (!qa_save_reader_init(&reader, value, "bot.state"))
    qa_range_error("Out of memory reading bot state checkpoint");
  size_t length = 0;
  field(&reader, "bytes", &cell);
  const uint8_t *bytes = qa_save_reader_bytes(&cell, &length);
  if (length != QA_BOT_STATE_SOURCE_BYTES)
    qa_save_reader_fail(&reader, "incorrect bot state byte extent");
  if (s->has_source_allocation && memcmp(bytes, s->src, QA_BOT_STATE_SOURCE_BYTES) != 0)
    qa_save_reader_fail(&reader, "bot bytes disagree with restored game allocation");
  qa_bot_activate_goal_bots_a *next[QA_MAX_ACTIVATESTACK];
  field(&reader, "activationNext", &inner);
  size_t count = qa_save_reader_list_count(&inner);
  for (size_t i = 0; i < count && i < QA_MAX_ACTIVATESTACK; i++) {
    qa_save_reader entry;
    item(&inner, i, &entry);
    next[i] = activation_at(s, &entry);
    qa_save_reader_destroy(&entry);
  }
  if (count != QA_MAX_ACTIVATESTACK)
    qa_save_reader_fail(&reader, "incorrect activation heap extent");
  qa_save_reader_destroy(&inner);

  field(&reader, "aiNode", &inner);
  qa_ai_node_bots_a node = QA_AI_NODE_NONE;
  if (!qa_save_reader_is_null(&inner))
    node = (qa_ai_node_bots_a)(choice(&inner, NODE_NAMES + 1, 11) + 1);
  qa_save_reader_destroy(&inner);

  qa_bot_setup_progress_bots_a progress;
  memset(&progress, 0, sizeof progress);
  field(&reader, "setup", &inner);
  qa_save_reader kind_reader, stage_reader;
  field(&inner, "kind", &kind_reader);
  progress.kind = (qa_bot_setup_kind_bots_a)choice(&kind_reader, SETUP_KINDS, 4);
  qa_save_reader_destroy(&kind_reader);
  if (progress.kind == QA_BOT_SETUP_SETTING_UP) {
    field(&inner, "stage", &stage_reader);
    progress.stage = (qa_bot_setup_stage_bots_a)choice(&stage_reader, STAGE_NAMES, 17);
    qa_save_reader_destroy(&stage_reader);
  } else if (progress.kind == QA_BOT_SETUP_FAILED) {
    static const char *const FAILED[] = {"aas", "character", "item-weights", "weapon-weights",
                                         "chat-file"};
    static const qa_bot_setup_stage_bots_a FAILED_STAGE[] = {
        QA_BOT_SETUP_AAS, QA_BOT_SETUP_CHARACTER, QA_BOT_SETUP_ITEM_WEIGHTS,
        QA_BOT_SETUP_WEAPON_WEIGHTS, QA_BOT_SETUP_CHAT_FILE};
    field(&inner, "stage", &stage_reader);
    int64_t index = choice(&stage_reader, FAILED, 5);
    qa_save_reader_destroy(&stage_reader);
    progress.stage = FAILED_STAGE[index];
    if (index >= 2) {
      field(&inner, "errorCode", &stage_reader);
      progress.error_code = (int64_t)qa_save_reader_integer(&stage_reader);
      qa_save_reader_destroy(&stage_reader);
    }
  }
  qa_save_reader_destroy(&inner);

  bool has_order = false;
  qa_bot_order_state order;
  memset(&order, 0, sizeof order);
  field(&reader, "scriptedOrder", &inner);
  if (!qa_save_reader_is_null(&inner)) {
    static const char *const ORDER_KINDS[] = {"point", "follow"};
    static const char *const PROGRESS[] = {"in-progress", "success", "error"};
    qa_save_reader order_reader, sub, leaf;
    has_order = true;
    field(&inner, "order", &order_reader);
    field(&order_reader, "kind", &sub);
    int64_t kind = choice(&sub, ORDER_KINDS, 2);
    qa_save_reader_destroy(&sub);
    field(&inner, "progress", &sub);
    order.progress = (qa_bot_order_progress)choice(&sub, PROGRESS, 3);
    qa_save_reader_destroy(&sub);
    if (kind == 0) {
      order.order.kind = QA_BOT_ORDER_POINT;
      field(&order_reader, "point", &sub);
      field(&sub, "x", &leaf);
      order.order.point.x = qa_save_reader_number(&leaf);
      qa_save_reader_destroy(&leaf);
      field(&sub, "y", &leaf);
      order.order.point.y = qa_save_reader_number(&leaf);
      qa_save_reader_destroy(&leaf);
      field(&sub, "z", &leaf);
      order.order.point.z = qa_save_reader_number(&leaf);
      qa_save_reader_destroy(&leaf);
      qa_save_reader_destroy(&sub);
    } else {
      int64_t number, generation;
      order.order.kind = QA_BOT_ORDER_FOLLOW;
      field(&order_reader, "entity", &sub);
      field(&sub, "number", &leaf);
      number = (int64_t)qa_save_reader_integer_min(&leaf, 0);
      qa_save_reader_destroy(&leaf);
      field(&sub, "generation", &leaf);
      generation = (int64_t)qa_save_reader_integer_min(&leaf, 0);
      qa_save_reader_destroy(&leaf);
      qa_save_reader_destroy(&sub);
      remap(remap_ctx, number, generation, &order.order.entity.number,
            &order.order.entity.generation);
    }
    qa_save_reader_destroy(&order_reader);
  }
  qa_save_reader_destroy(&inner);

  memcpy(s->src, bytes, QA_BOT_STATE_SOURCE_BYTES);
  qa_save_reader_destroy(&cell);
  s->ai_node = node;
  s->setup = progress;
  s->has_scripted_order = has_order;
  s->scripted_order = order;
  field(&reader, "activateStack", &inner);
  s->activate_stack = activation_at(s, &inner);
  qa_save_reader_destroy(&inner);
  for (int i = 0; i < QA_MAX_ACTIVATESTACK; i++)
    s->activate_goal_heap[i].next = next[i];
  s->checkpoints = waypoint(waypoint_ctx, nullable_integer(&reader, "checkpoints"));
  s->patrol_points = waypoint(waypoint_ctx, nullable_integer(&reader, "patrolPoints"));
  s->current_patrol_point = waypoint(waypoint_ctx, nullable_integer(&reader, "currentPatrolPoint"));
  qa_save_reader_destroy(&reader);
}

void qa_bot_state_store_bots_a_init(qa_bot_state_store_bots_a *st, qa_q3_base_product product) {
  memset(st, 0, sizeof *st);
  st->product = product;
}
static void retire(qa_bot_state_store_bots_a *st, qa_bot_state_bots_a *state) {
  if (st->retired_count == st->retired_capacity) {
    size_t capacity = st->retired_capacity == 0 ? 8 : st->retired_capacity * 2;
    qa_bot_state_bots_a **grown = realloc(st->retired, capacity * sizeof *grown);
    if (grown == NULL)
      qa_range_error("Out of memory retiring a bot state");
    st->retired = grown;
    st->retired_capacity = capacity;
  }
  st->retired[st->retired_count++] = state;
}
static void release(qa_bot_state_bots_a *state) {
  qa_bot_state_bots_a_destroy(state);
  free(state);
}
void qa_bot_state_store_bots_a_destroy(qa_bot_state_store_bots_a *st) {
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++)
    if (st->cells[i] != NULL)
      release(st->cells[i]);
  for (size_t i = 0; i < st->retired_count; i++)
    release(st->retired[i]);
  free(st->retired);
  memset(st, 0, sizeof *st);
}
qa_bot_state_bots_a *qa_bot_state_store_get(qa_bot_state_store_bots_a *st, int64_t client) {
  if (client < 0 || client >= QA_Q3_MAX_CLIENTS)
    qa_range_error("Bot client %lld outside %d slots", (long long)client, QA_Q3_MAX_CLIENTS);
  return st->cells[client];
}
static qa_bot_state_bots_a *new_state(qa_q3_base_product product,
                                      const qa_q3_game_memory_allocation *allocation) {
  qa_bot_state_bots_a *state = malloc(sizeof *state);
  if (state == NULL || !qa_bot_state_bots_a_init(state, product, allocation))
    qa_range_error("Out of memory allocating a bot state");
  return state;
}
qa_bot_state_bots_a *qa_bot_state_store_acquire(qa_bot_state_store_bots_a *st, int64_t client,
                                                qa_q3_game_memory *memory) {
  qa_bot_state_bots_a *state = qa_bot_state_store_get(st, client);
  if (state != NULL)
    return state;
  qa_q3_game_memory_allocation allocation =
      qa_q3_game_memory_allocate(memory, QA_BOT_STATE_SOURCE_BYTES);
  state = new_state(st->product, &allocation);
  st->cells[client] = state;
  return state;
}
qa_save_value *qa_bot_state_store_capture_save_state(qa_bot_state_store_bots_a *st,
                                                     qa_q3_game_memory *memory,
                                                     qa_bot_waypoint_id_fn waypoint_id, void *ctx) {
  qa_save_value *out = sv(qa_save_value_array());
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) {
    qa_bot_state_bots_a *state = st->cells[i];
    if (state == NULL) {
      arr_push(out, sv(qa_save_value_null()));
      continue;
    }
    qa_save_value *entry = sv(qa_save_value_object());
    if (!state->has_source_allocation)
      obj_set(entry, "allocation", sv(qa_save_value_null()));
    else {
      qa_q3_game_memory_pointer pointer =
          qa_q3_game_memory_capture_allocation(memory, state->source_allocation);
      qa_save_value *allocation = sv(qa_save_value_object());
      obj_set(allocation, "offset", sv(qa_save_value_number(pointer.offset)));
      obj_set(allocation, "length", sv(qa_save_value_number(pointer.length)));
      obj_set(entry, "allocation", allocation);
    }
    obj_set(entry, "state", qa_bot_state_capture_save_state(state, waypoint_id, ctx));
    arr_push(out, entry);
  }
  return out;
}
void qa_bot_state_store_restore_save_state(qa_bot_state_store_bots_a *st, const qa_save_value *value,
                                           qa_q3_game_memory *memory,
                                           qa_bot_waypoint_at_fn waypoint, void *waypoint_ctx,
                                           qa_bot_remap_observation_fn remap, void *remap_ctx) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "bot.states"))
    qa_range_error("Out of memory reading bot state checkpoint");
  size_t count = qa_save_reader_list_count(&reader);
  if (count != QA_Q3_MAX_CLIENTS)
    qa_save_reader_fail(&reader, "incorrect bot state slot count");
  qa_bot_state_bots_a *states[QA_Q3_MAX_CLIENTS];
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, cell;
    item(&reader, i, &entry);
    states[i] = NULL;
    if (!qa_save_reader_is_null(&entry)) {
      field(&entry, "allocation", &cell);
      if (qa_save_reader_is_null(&cell))
        states[i] = new_state(st->product, NULL);
      else {
        qa_q3_game_memory_allocation allocation =
            qa_q3_game_memory_restore_allocation(memory, cell.value);
        states[i] = new_state(st->product, &allocation);
      }
      qa_save_reader_destroy(&cell);
      field(&entry, "state", &cell);
      qa_bot_state_restore_save_state(states[i], cell.value, waypoint, waypoint_ctx, remap,
                                      remap_ctx);
      qa_save_reader_destroy(&cell);
    }
    qa_save_reader_destroy(&entry);
  }
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) {
    if (st->cells[i] != NULL)
      retire(st, st->cells[i]);
    st->cells[i] = states[i];
  }
  qa_save_reader_destroy(&reader);
}
void qa_bot_state_store_clear(qa_bot_state_store_bots_a *st) {
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) {
    if (st->cells[i] != NULL)
      retire(st, st->cells[i]);
    st->cells[i] = NULL;
  }
}

static void copy_slots(int32_t *target, size_t count, const qa_q3_base_player_state_slots *slots) {
  for (size_t i = 0; i < count; i++)
    target[i] = to_int32(qa_q3_base_player_state_slots_get(slots, (double)i));
}
void qa_copy_bot_player_state_bots_a(qa_bot_state_bots_a *destination,
                                     const qa_q3_base_player_state *p) {
  if (destination->product != qa_q3_base_player_state_record_product(p))
    qa_range_error("Bot player-state product differs from its client state");
  qa_bot_player_state_source *d = &destination->src->cur_ps;
  d->command_time = to_int32(p->command_time);
  d->pm_type = to_int32(p->pm_type);
  d->bob_cycle = to_int32(p->bob_cycle);
  d->pm_flags = to_int32(p->pm_flags);
  d->pm_time = to_int32(p->pm_time);
  d->origin = qa_bot_fv3(qa_q3_base_player_state_record_origin(p));
  d->velocity = qa_bot_fv3(qa_q3_base_player_state_record_velocity(p));
  d->weapon_time = to_int32(p->weapon_time);
  d->gravity = to_int32(p->gravity);
  d->speed = to_int32(p->speed);
  d->delta_angles = (qa_bot_ivec3){to_int32(p->delta_angles.x), to_int32(p->delta_angles.y),
                                   to_int32(p->delta_angles.z)};
  d->ground_entity_num = to_int32(p->ground_entity_num);
  d->legs_timer = to_int32(p->legs_timer);
  d->legs_anim = to_int32(p->legs_anim);
  d->torso_timer = to_int32(p->torso_timer);
  d->torso_anim = to_int32(p->torso_anim);
  d->movement_dir = to_int32(p->movement_dir);
  d->grapple_point = qa_bot_fv3(p->grapple_point);
  d->e_flags = to_int32(p->e_flags);
  d->event_sequence = to_int32(p->event_sequence);
  copy_slots(d->events, 2, &p->events);
  copy_slots(d->event_parms, 2, &p->event_parms);
  d->external_event = to_int32(p->external_event);
  d->external_event_parm = to_int32(p->external_event_parm);
  d->external_event_time = to_int32(p->external_event_time);
  d->client_num = to_int32(p->client_num);
  d->weapon = to_int32(p->weapon);
  d->weapon_state = to_int32(p->weapon_state);
  d->viewangles = qa_bot_fv3(p->viewangles);
  d->viewheight = to_int32(p->viewheight);
  d->damage_event = to_int32(p->damage_event);
  d->damage_yaw = to_int32(p->damage_yaw);
  d->damage_pitch = to_int32(p->damage_pitch);
  d->damage_count = to_int32(p->damage_count);
  copy_slots(d->stats, 16, &p->stats);
  copy_slots(d->persistant, 16, &p->persistant);
  copy_slots(d->powerups, 16, &p->powerups);
  copy_slots(d->ammo, 16, &p->ammo);
  d->generic1 = to_int32(p->generic1);
  d->loop_sound = to_int32(p->loop_sound);
  d->jumppad_ent = to_int32(p->jumppad_ent);
  d->ping = to_int32(p->ping);
  d->pmove_framecount = to_int32(p->pmove_framecount);
  d->jumppad_frame = to_int32(p->jumppad_frame);
  d->entity_event_sequence = to_int32(p->entity_event_sequence);
}
