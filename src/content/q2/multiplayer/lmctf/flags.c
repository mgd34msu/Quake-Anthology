/* Port of src/content/q2/multiplayer/lmctf/flags.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/flags.h"
#include "../multiplayer_internal.h"
#include "qa/content/q2/multiplayer/ctf/types.h" /* CTF_FLAGS, otherCtfTeam */
#include "qa/foundation/js.h"

static const qa_bounds FLAG_BOUNDS = {{-15, -15, -15}, {15, 15, 33}};

struct qa_lmctf_flags {
  qa_lmctf_context *context;
  bool has_flag[3];
  qa_actor_id flag_actors[3];
  double last_taken_sound;
  qa_q2_named_think thinks[2];
  qa_q2_named_touch touches[2];
  qa_q2_callback_definitions callbacks;
};

static void properties(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g);
static bool pickup_impl(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g, qa_actor_id actor);
static qa_lmctf_playing_team flag_team_of(qa_q2_entity *flag) {
  if (flag->count != 1 && flag->count != 2) qa_raise("Error", 0, 0, "LMCTF flag has no source team");
  return flag->count;
}

static void wave_fn(void *ctx, qa_q2_entity *flag, qa_q2_game_services *g) {
  qa_lmctf_flags *f = ctx;
  if (strcmp(flag->solid, "none") != 0) {
    flag->frame = 173 + fmod(flag->frame - 172, 16);
    g->show(g->ctx, flag);
  }
  qa_q2_think self = {f, wave_fn};
  g->schedule(g->ctx, flag, 0.1, self);
  bool owner_inactive = !flag->has_owner || !qa_lmctf_active(f->context, g, flag->owner);
  if (flag->timestamp != 0 && qa_q2mp_now(g) > flag->timestamp + 30 && owner_inactive) {
    g->sound(g->ctx, flag->actor, flag_team_of(flag) == 1 ? "ctf/r_returned.wav" : "ctf/b_returned.wav", 5, 0.8, 0);
    qa_lmctf_flags_reset(f, flag, NULL, g);
  }
}

static void touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  pickup_impl(ctx, entity, g, contact.other);
}
static void drop_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  if (entity->has_owner && qa_same_actor(entity->owner, contact.other)) return;
  pickup_impl(ctx, entity, g, contact.other);
}
static void dropped_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_flags *f = ctx;
  entity->has_owner = false;
  entity->touch = (qa_q2_touch){f, touch_fn};
  qa_q2_think wave = {f, wave_fn};
  g->schedule(g->ctx, entity, 0.1, wave);
}

static bool item_use(void *ctx, qa_owned_actor actor, qa_q2_game_services *g) {
  qa_lmctf_flags *f = ctx;
  qa_q2_entity *player = qa_q2_game_entity_of(g, &actor.id);
  if (player == NULL || qa_lmctf_flags_carried(f, actor.id, g) == NULL) return false;
  qa_lmctf_flags_drop(f, player, g);
  return true;
}
static bool item_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player) {
  pickup_impl(ctx, entity, g, player.id);
  return false;
}

qa_lmctf_flags *qa_lmctf_flags_create(qa_lmctf_context *context) {
  qa_lmctf_flags *f = calloc(1, sizeof *f);
  if (f == NULL) return NULL;
  f->context = context;
  f->thinks[0] = (qa_q2_named_think){"lmctf:ctf_flagwave", {f, wave_fn}};
  f->thinks[1] = (qa_q2_named_think){"lmctf:Drop_Flag_Think", {f, dropped_think_fn}};
  f->touches[0] = (qa_q2_named_touch){"lmctf:ctf_flagtouch", {f, touch_fn}};
  f->touches[1] = (qa_q2_named_touch){"lmctf:Flag_DropTouch", {f, drop_touch_fn}};
  f->callbacks.think = f->thinks; f->callbacks.think_count = 2;
  f->callbacks.touch = f->touches; f->callbacks.touch_count = 2;
  qa_q2_item_definition def = {0};
  def.kind = "custom";
  def.classname = "flag";
  def.model = "players/male/flag1.md2";
  def.icon = "a_redflag";
  def.name = "Enemy Flag";
  def.sound = "misc/am_pkup.wav";
  def.rotate = false;
  def.respawn = 0;
  def.capacity = 32767;
  def.quantity = 1;
  def.has_coop_stay = true;
  def.coop_stay = false;
  def.droppable = false;
  def.custom_ctx = f;
  def.has_use = true;
  def.use = item_use;
  def.pickup = item_pickup;
  qa_q2_item_module_register(context->hooks.items, def);
  return f;
}
void qa_lmctf_flags_destroy(qa_lmctf_flags *f) { free(f); }
qa_q2_callback_definitions qa_lmctf_flags_callbacks(qa_lmctf_flags *f) { return f->callbacks; }

void qa_lmctf_flags_capture(qa_lmctf_flags *f, qa_lmctf_flags_checkpoint *out) {
  size_t count = 0;
  for (int team = 1; team <= 2; team++)
    if (f->has_flag[team]) count++;
  out->flags = qa_q2mp_xalloc(malloc((count == 0 ? 1 : count) * sizeof *out->flags));
  out->flag_count = count;
  size_t at = 0;
  for (int team = 1; team <= 2; team++)
    if (f->has_flag[team]) {
      out->flags[at].team = team;
      out->flags[at].actor = qa_save_ctf_actor(f->flag_actors[team]);
      at++;
    }
  out->last_taken_sound = f->last_taken_sound;
}

void qa_lmctf_flags_restore(qa_lmctf_flags *f, qa_q2_game_services *g, qa_lmctf_flags_checkpoint saved) {
  f->has_flag[1] = false;
  f->has_flag[2] = false;
  for (size_t i = 0; i < saved.flag_count; i++) {
    qa_owned_actor owned;
    if (!qa_session_actor_registry_resolve_saved(g->host->actors, saved.flags[i].actor, &owned))
      qa_raise("Error", 0, 0, "Saved LMCTF flag is absent");
    f->has_flag[(int)saved.flags[i].team] = true;
    f->flag_actors[(int)saved.flags[i].team] = owned.id;
  }
  f->last_taken_sound = saved.last_taken_sound;
}

qa_q2_entity *qa_lmctf_flags_flag(qa_lmctf_flags *f, qa_lmctf_playing_team team, qa_q2_game_services *g) {
  if (!f->has_flag[(int)team]) return NULL;
  return qa_q2_game_entity_of(g, &f->flag_actors[(int)team]);
}

qa_q2_entity *qa_lmctf_flags_carried(qa_lmctf_flags *f, qa_actor_id actor, qa_q2_game_services *g) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(f->context, actor);
  if (state == NULL || state->team == 0 || qa_q2mp_inv_count(g, actor, "q2:flag") == 0) return NULL;
  return qa_lmctf_flags_flag(f, qa_other_ctf_team((qa_q2_ctf_playing_team)state->team), g);
}

bool qa_lmctf_flags_at_home(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g) {
  (void)f;
  return qa_q2_length(qa_q2_subtract(flag->pos1, qa_q2_game_body(g, flag).origin)) <= 32;
}

const char *qa_lmctf_flags_state(qa_lmctf_flags *f, qa_lmctf_playing_team team, qa_q2_game_services *g) {
  qa_q2_entity *flag = qa_lmctf_flags_flag(f, team, g);
  if (flag == NULL || (qa_lmctf_flags_at_home(f, flag, g) && strcmp(flag->solid, "none") != 0)) return "base";
  return strcmp(flag->solid, "none") == 0 ? "taken" : "dropped";
}

static void properties(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g) {
  flag->visible = true;
  flag->has_owner = false;
  flag->timestamp = 0;
  flag->touch = (qa_q2_touch){f, touch_fn};
  qa_q2_body_changes move = {0};
  move.has_bounds = true;
  move.bounds = FLAG_BOUNDS;
  qa_q2_game_move(g, flag, move, true);
  g->solid(g->ctx, flag, "trigger");
  g->motion(g->ctx, flag, "toss");
  qa_vec3 origin = qa_q2_game_body(g, flag).origin;
  qa_bounds bounds = FLAG_BOUNDS;
  qa_trace_result trace = qa_q2mp_trace(g, origin, qa_q2_add(origin, (qa_vec3){0, 0, -128}), &bounds, &flag->actor.id, 3);
  qa_q2_body_changes place = {0};
  place.has_origin = true; place.origin = trace.end;
  place.has_angles = true; place.angles = QA_Q2_ZERO;
  place.has_velocity = true; place.velocity = QA_Q2_ZERO;
  qa_q2_game_move(g, flag, place, true);
  g->show(g->ctx, flag);
  qa_q2_think wave = {f, wave_fn};
  g->schedule(g->ctx, flag, 0.1, wave);
}

void qa_lmctf_flags_reset(qa_lmctf_flags *f, qa_q2_entity *flag, const qa_actor_id *player, qa_q2_game_services *g) {
  if (flag != NULL) {
    qa_q2_body_changes move = {0};
    move.has_origin = true; move.origin = flag->pos1;
    move.has_angles = true; move.angles = flag->pos2;
    qa_q2_game_move(g, flag, move, true);
    properties(f, flag, g);
  }
  qa_q2_entity *entity = player == NULL ? NULL : qa_q2_game_entity_of(g, player);
  if (entity != NULL) {
    entity->effects = (double)(qa_js_to_int32(entity->effects) & ~0x100);
    entity->model3 = "";
    qa_shared_inventory_table_consume(g->host->inventory, entity->actor, "q2:flag",
                                      qa_q2mp_inv_count(g, entity->actor.id, "q2:flag"));
    g->show(g->ctx, entity);
  }
}

void qa_lmctf_flags_reset_all(qa_lmctf_flags *f, qa_q2_game_services *g) {
  for (int team = 1; team <= 2; team++) {
    qa_q2_entity *flag = qa_lmctf_flags_flag(f, team, g);
    qa_actor_id owner;
    const qa_actor_id *owner_ptr = NULL;
    if (flag != NULL && flag->has_owner) {
      owner = flag->owner;
      owner_ptr = &owner;
    }
    qa_lmctf_flags_reset(f, flag, owner_ptr, g);
  }
}

static qa_q2_entity *farthest(qa_lmctf_flags *f, qa_q2_entity *from, qa_q2_game_services *g) {
  (void)f;
  size_t dm_n = 0, red_n = 0, blue_n = 0;
  qa_q2_entity **dm = qa_q2mp_entities_named(g, "info_player_deathmatch", &dm_n);
  qa_q2_entity **red = qa_q2mp_entities_named(g, "info_flag_red", &red_n);
  qa_q2_entity **blue = qa_q2mp_entities_named(g, "info_flag_blue", &blue_n);
  qa_q2_entity *best = NULL;
  double distance = 0;
  qa_vec3 from_origin = qa_q2_game_body(g, from).origin;
  for (size_t i = 0; i < dm_n; i++) {
    double current = qa_q2_length(qa_q2_subtract(from_origin, qa_q2_game_body(g, dm[i]).origin));
    if (current > distance) { best = dm[i]; distance = current; }
  }
  if (red_n > 0) {
    double current = qa_q2_length(qa_q2_subtract(from_origin, qa_q2_game_body(g, red[0]).origin));
    if (current > distance) { best = red[0]; distance = current; }
  }
  if (blue_n > 0) {
    double current = qa_q2_length(qa_q2_subtract(from_origin, qa_q2_game_body(g, blue[0]).origin));
    if (current > distance) { best = blue[0]; distance = current; }
  }
  qa_q2_entity *fallback = dm_n > 0 ? dm[0] : NULL;
  qa_q2_entity *out = best != NULL ? best : fallback;
  free(dm);
  free(red);
  free(blue);
  return out;
}

static qa_q2_entity *first_named(qa_q2_game_services *g, const char *classname) {
  size_t n = 0;
  qa_q2_entity **spots = qa_q2mp_entities_named(g, classname, &n);
  qa_q2_entity *out = n > 0 ? spots[0] : NULL;
  free(spots);
  return out;
}

static void spawn_flag(qa_lmctf_flags *f, qa_lmctf_playing_team team, qa_q2_game_services *g) {
  if (qa_lmctf_flags_flag(f, team, g) != NULL || g->options.mode != QA_Q2_MODE_DEATHMATCH ||
      (qa_js_to_int32(f->context->rules->ctf_flags) & 256) != 0) return;
  const char *classname = team == 1 ? "info_flag_red" : "info_flag_blue";
  qa_q2_entity *spot = first_named(g, classname);
  if (spot == NULL) {
    spot = first_named(g, "info_player_deathmatch");
    if (spot != NULL) spot = farthest(f, spot, g);
    if (team == 2 && spot != NULL) spot = farthest(f, spot, g);
    if (spot == NULL) spot = first_named(g, team == 1 ? "info_player_start" : "target_changelevel");
    if (spot == NULL) return;
    spot->classname = classname;
    spot->effects = (double)(qa_js_to_int32(spot->effects) | 0x100);
    spot->render_flags = (double)(qa_js_to_int32(spot->render_flags) | (team == 1 ? 1024 : 4096));
    g->show(g->ctx, spot);
  }
  qa_q2_entity *flag = g->create(g->ctx, "flag", NULL, 0);
  qa_body_state body = qa_q2_game_body(g, spot);
  flag->count = team;
  flag->model = QA_CTF_FLAGS[(int)team].model;
  flag->effects = QA_CTF_FLAGS[(int)team].effect;
  flag->frame = f->context->rules->flag_init ? 173 : 0;
  flag->pos1 = body.origin;
  flag->pos2 = body.angles;
  f->has_flag[(int)team] = true;
  f->flag_actors[(int)team] = flag->actor.id;
  qa_lmctf_flags_reset(f, flag, NULL, g);
  properties(f, flag, g);
  size_t n = 0;
  qa_q2_entity **spawns = qa_q2mp_entities_named(g, "info_player_deathmatch", &n);
  for (size_t i = 0; i < n; i++)
    if (qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, spawns[i]).origin, flag->pos1)) <= 256) g->remove(g->ctx, spawns[i]);
  free(spawns);
}

bool qa_lmctf_flags_spawn(qa_lmctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (strcmp(entity->classname, "info_flag_red") == 0 || strcmp(entity->classname, "info_flag_blue") == 0) {
    g->solid(g->ctx, entity, "none");
    return true;
  }
  if (strcmp(entity->classname, "flag") != 0) return false;
  if (entity->count == 1 || entity->count == 2) {
    f->has_flag[(int)entity->count] = true;
    f->flag_actors[(int)entity->count] = entity->actor.id;
    properties(f, entity, g);
  }
  return true;
}

void qa_lmctf_flags_post_spawn(qa_lmctf_flags *f, qa_q2_game_services *g) {
  qa_q2_source_callbacks_register(g->source_callbacks, f->callbacks);
  spawn_flag(f, 1, g);
  spawn_flag(f, 2, g);
}

static void announce(qa_lmctf_flags *f, qa_q2_game_services *g, qa_lmctf_playing_team team, const char *own, const char *other) {
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_lmctf_player_state *state = qa_lmctf_context_get(f->context, players[i]);
    qa_lmctf_print(g, &players[i], "%s", state != NULL && state->team == team ? own : other);
  }
}

static void flag_sound(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g, const char *path, double volume) {
  (void)f;
  g->sound(g->ctx, flag->actor, path, 5, volume, 0);
}

void qa_lmctf_flags_drop(qa_lmctf_flags *f, qa_q2_entity *player, qa_q2_game_services *g) {
  qa_q2_entity *flag = qa_lmctf_flags_carried(f, player->actor.id, g);
  if (flag == NULL) return;
  qa_lmctf_flags_reset(f, flag, &player->actor.id, g);
  properties(f, flag, g);
  flag->has_owner = true;
  flag->owner = player->actor.id;
  flag->touch = (qa_q2_touch){f, drop_touch_fn};
  qa_q2_think think = {f, dropped_think_fn};
  g->schedule(g->ctx, flag, 1, think);
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, player->actor.id);
  qa_vec3 forward = qa_math_angle_vectors_rule(view.has_value ? view.value.view_angles : qa_q2_game_body(g, player).angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_lmctf_toss(flag, player, g, forward);
  flag->timestamp = qa_q2mp_now(g);
  qa_lmctf_stat(f->context, player->actor.id, "flag-lost", 1);
  qa_lmctf_score(f->context, g, player->actor.id, 0, "FC LostFlag", NULL);
  const char * value2 = qa_lmctf_name(f->context, player->actor.id);
  char * value3 = flag_team_of(flag) == 1 ? "red" : "blue";
  qa_lmctf_print(g, NULL, "%s lost the %s flag.\n", value2, value3);
}

static void capture_flag(qa_lmctf_flags *f, qa_q2_entity *home, qa_q2_game_services *g, qa_q2_entity *player, qa_lmctf_playing_team team) {
  qa_q2_entity *enemy = qa_lmctf_flags_flag(f, qa_other_ctf_team(team), g);
  if (enemy == NULL) return;
  const char *name = qa_lmctf_name(f->context, player->actor.id);
  const char *color = team == 1 ? "blue" : "red";
  double now = qa_q2mp_now(g);
  char own[512], other[512];
  snprintf(own, sizeof own, "%s captured your flag!\n", name);
  snprintf(other, sizeof other, "%s captured the %s flag.\n", name, color);
  announce(f, g, qa_other_ctf_team(team), own, other);
  struct assist { int field; double window; const char *log_name; const char *reason; };
  static const struct assist assists[3] = {
      {0, 6, "FC Frag Assist", "killing the flag carrier"},
      {1, 3, "F Return Assist", "returning the flag"},
      {2, 2, "F Defend Assist", "defending the flag"},
  };
  for (size_t i = 0; i < f->context->state_count; i++) {
    qa_actor_id actor = f->context->states[i].actor;
    qa_lmctf_player_state *member = &f->context->states[i].state;
    if (member->team != team) continue;
    for (int a = 0; a < 3; a++) {
      double *field = assists[a].field == 0 ? &member->kill_carrier_time
                      : assists[a].field == 1 ? &member->return_flag_time : &member->defend_flag_time;
      if (now < *field + assists[a].window) {
        qa_lmctf_print(g, NULL, "%s assisted the capture by %s.\n", qa_lmctf_name(f->context, actor), assists[a].reason);
        qa_lmctf_score(f->context, g, actor, 1, assists[a].log_name, NULL);
        qa_lmctf_stat(f->context, actor, "assists", 1);
        *field = 0;
      }
    }
  }
  char path[64];
  char skin[32];
  qa_q2mp_format_number(skin, sizeof skin, f->context->rules->skin_set + 1);
  snprintf(path, sizeof path, "ctf/%sscore%s.wav", team == 1 ? "red" : "blue", skin);
  flag_sound(f, home, g, path, 1);
  qa_q2_presentation_event event = {0};
  event.kind = "effect";
  event.effect = "bfg-explosion";
  event.origin = qa_q2_game_body(g, home).origin;
  event.direction = QA_Q2_ZERO;
  event.count = 0;
  event.color = 0;
  g->host->emit(g->host->ctx, event);
  qa_lmctf_score(f->context, g, player->actor.id, 5, "F Capture", NULL);
  qa_lmctf_stat(f->context, player->actor.id, "captures", 1);
  double bonus = 10;
  if ((qa_js_to_int32(f->context->rules->ctf_flags) & 512) != 0) {
    double allies = 1, enemies = 1;
    for (size_t i = 0; i < f->context->state_count; i++) {
      qa_actor_id actor = f->context->states[i].actor;
      qa_lmctf_player_state *state = &f->context->states[i].state;
      if (qa_lmctf_active(f->context, g, actor)) {
        if (state->team == team) allies++;
        else if (state->team != 0) enemies++;
      }
    }
    bonus = trunc(bonus * enemies / allies);
  }
  for (size_t i = 0; i < f->context->state_count; i++)
    if (f->context->states[i].state.team == team)
      qa_lmctf_score(f->context, g, f->context->states[i].actor, bonus, "Team Score", NULL);
  qa_actor_id owner;
  const qa_actor_id *owner_ptr = NULL;
  if (enemy->has_owner) {
    owner = enemy->owner;
    owner_ptr = &owner;
  }
  qa_lmctf_flags_reset(f, enemy, owner_ptr, g);
}

static bool pickup_impl(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g, qa_actor_id actor) {
  if (!qa_lmctf_context_flags_touchable(f->context) || !qa_lmctf_active(f->context, g, actor) ||
      qa_q2mp_health_or(g, actor, 0) <= 0 || strcmp(flag->solid, "trigger") != 0) return false;
  qa_lmctf_playing_team team = flag_team_of(flag);
  qa_q2_entity *player = qa_q2_game_entity_of(g, &actor);
  qa_lmctf_player_state *state = qa_lmctf_player(f->context, actor);
  const char *name = qa_lmctf_name(f->context, actor);
  const char *color = team == 1 ? "red" : "blue";
  double now = qa_q2mp_now(g);
  if (player == NULL) return false;
  if (state->team == team) {
    if (qa_lmctf_flags_at_home(f, flag, g)) {
      if (qa_q2mp_inv_count(g, actor, "q2:flag") > 0) capture_flag(f, flag, g, player, team);
    } else {
      flag_sound(f, flag, g, team == 1 ? "ctf/r_returned.wav" : "ctf/b_returned.wav", 0.8);
      qa_lmctf_stat(f->context, actor, "returns", 1);
      qa_lmctf_score(f->context, g, actor, 1, "F Return", NULL);
      state->return_flag_time = now;
      char own[512], other[512];
      snprintf(own, sizeof own, "%s returned your flag!\n", name);
      snprintf(other, sizeof other, "%s returned the %s flag.\n", name, color);
      announce(f, g, team, own, other);
      for (size_t i = 0; i < f->context->state_count; i++) {
        qa_actor_id other_actor = f->context->states[i].actor;
        qa_lmctf_player_state *member = &f->context->states[i].state;
        if (!qa_same_actor(other_actor, actor) && member->team == team && now < member->kill_carrier_time + 6) {
          qa_lmctf_print(g, NULL, "%s helped %s return the %s flag.\n", qa_lmctf_name(f->context, other_actor), name, color);
          qa_lmctf_score(f->context, g, other_actor, 1, "F Return Assist", NULL);
          qa_lmctf_stat(f->context, other_actor, "assists", 1);
          member->kill_carrier_time = 0;
        }
      }
      qa_lmctf_flags_reset(f, flag, NULL, g);
    }
    return false;
  }
  if ((qa_js_to_int32(f->context->rules->ref_flags) & qa_js_to_int32(team)) != 0) return false;
  player->effects = (double)(qa_js_to_int32(player->effects) | 0x100);
  player->render_flags = (double)(qa_js_to_int32(player->render_flags) | (team == 2 ? 1024 : 4096));
  char own[512], other[512];
  snprintf(own, sizeof own, "%s stole your flag!\n", name);
  snprintf(other, sizeof other, "%s stole the %s flag.\n", name, color);
  announce(f, g, team, own, other);
  qa_lmctf_stat(f->context, actor, "flag-taken", 1);
  qa_lmctf_score(f->context, g, actor, 0, "F Pickup", NULL);
  if (qa_lmctf_flags_at_home(f, flag, g)) {
    g->sound(g->ctx, flag->actor, "ctf/flagtk.wav", 0, 0.7, 1);
    flag_sound(f, flag, g, team == 1 ? "ctf/r_stolen.wav" : "ctf/b_stolen.wav", 0.8);
  } else if (now > f->last_taken_sound + 8) {
    f->last_taken_sound = now;
    flag_sound(f, flag, g, team == 1 ? "ctf/r_stolen.wav" : "ctf/b_stolen.wav", 0.8);
  }
  flag->has_owner = true;
  flag->owner = actor;
  flag->visible = false;
  g->solid(g->ctx, flag, "none");
  g->show(g->ctx, flag);
  qa_q2_think wave = {f, wave_fn};
  g->schedule(g->ctx, flag, 0.1, wave);
  player->model3 = flag->model;
  g->show(g->ctx, player);
  size_t en = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &en);
  bool has_entry = false;
  for (size_t i = 0; i < en; i++)
    if (entries[i].item != NULL && strcmp(entries[i].item, "q2:flag") == 0) has_entry = true;
  if (!has_entry) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:flag"; entry.count = 0; entry.capacity = 32767;
    qa_shared_inventory_table_configure(g->host->inventory, player->actor, entry, NULL, NULL);
  }
  qa_shared_inventory_table_give(g->host->inventory, player->actor, "q2:flag", 1);
  qa_q2_presentation_event event = {0};
  event.kind = "pickup";
  event.player = actor;
  event.item = "q2:flag";
  event.name = "Enemy Flag";
  event.icon = "a_redflag";
  g->host->emit(g->host->ctx, event);
  g->sound(g->ctx, player->actor, "misc/am_pkup.wav", 3, 1, 1);
  return true;
}

bool qa_lmctf_flags_pickup(qa_lmctf_flags *f, qa_q2_entity *flag, qa_q2_game_services *g, qa_actor_id actor) {
  return pickup_impl(f, flag, g, actor);
}

void qa_lmctf_flags_hurt_carrier(qa_lmctf_flags *f, qa_actor_id target, const qa_actor_id *attacker, qa_q2_game_services *g) {
  if (attacker != NULL && qa_lmctf_flags_carried(f, target, g) != NULL) {
    qa_lmctf_player_state *state = qa_lmctf_context_get(f->context, *attacker);
    if (state != NULL) state->hit_carrier_time = qa_q2mp_now(g);
  }
}

void qa_lmctf_flags_frag(qa_lmctf_flags *f, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g) {
  qa_lmctf_player_state *source = qa_lmctf_context_get(f->context, attacker->actor.id);
  qa_lmctf_player_state *target = qa_lmctf_context_get(f->context, victim->actor.id);
  if (source == NULL || target == NULL || source->team == 0 || target->team == 0 || source->team == target->team || attacker == victim) return;
  qa_q2_entity *own_flag = qa_lmctf_flags_flag(f, (qa_lmctf_playing_team)source->team, g);
  qa_q2_entity *enemy_flag = qa_lmctf_flags_flag(f, (qa_lmctf_playing_team)target->team, g);
  double now = qa_q2mp_now(g);
  const char *color = source->team == 1 ? "red" : "blue";
  qa_vec3 attacker_origin = qa_q2_game_body(g, attacker).origin;
  qa_vec3 victim_origin = qa_q2_game_body(g, victim).origin;
  const char *attacker_name = qa_lmctf_name(f->context, attacker->actor.id);
  if (own_flag != NULL && (!own_flag->has_owner || !qa_lmctf_active(f->context, g, own_flag->owner))) {
    if (qa_lmctf_flags_at_home(f, own_flag, g)) {
      qa_vec3 origin = qa_q2_game_body(g, own_flag).origin;
      if (qa_q2_length(qa_q2_subtract(attacker_origin, origin)) < 800 || qa_q2_length(qa_q2_subtract(victim_origin, origin)) < 800) {
        qa_lmctf_score(f->context, g, attacker->actor.id, 2, "F Def", NULL);
        qa_lmctf_stat(f->context, attacker->actor.id, "defense-flag", 1);
        qa_lmctf_print(g, NULL, "%s defends the %s flag.\n", attacker_name, color);
        source->defend_flag_time = now;
      }
    } else {
      if (qa_q2_length(qa_q2_subtract(attacker_origin, own_flag->pos1)) < 600 || qa_q2_length(qa_q2_subtract(victim_origin, own_flag->pos1)) < 600) {
        qa_lmctf_score(f->context, g, attacker->actor.id, 1, "F Base Def", NULL);
        qa_lmctf_stat(f->context, attacker->actor.id, "defense-base", 1);
        qa_lmctf_print(g, NULL, "%s defends the %s base.\n", attacker_name, color);
      }
      qa_vec3 origin = qa_q2_game_body(g, own_flag).origin;
      if (qa_q2_length(qa_q2_subtract(attacker_origin, origin)) < 400 || qa_q2_length(qa_q2_subtract(victim_origin, origin)) < 400)
        source->defend_flag_time = now;
    }
    qa_q2_entity *carrier = NULL;
    if (enemy_flag != NULL && enemy_flag->has_owner) carrier = qa_q2_game_entity_of(g, &enemy_flag->owner);
    if (carrier != NULL && carrier != attacker && qa_lmctf_active(f->context, g, carrier->actor.id)) {
      if (now < target->hit_carrier_time + 2) {
        qa_lmctf_score(f->context, g, attacker->actor.id, 3, "FC Def", NULL);
        qa_lmctf_stat(f->context, attacker->actor.id, "defense-carrier", 1);
        qa_lmctf_print(g, NULL, "%s defends the %s flag carrier from an aggressive enemy.\n", attacker_name, color);
      } else {
        qa_vec3 origin = qa_q2_game_body(g, carrier).origin;
        if (qa_q2_length(qa_q2_subtract(attacker_origin, origin)) < 500 || qa_q2_length(qa_q2_subtract(victim_origin, origin)) < 500) {
          qa_lmctf_score(f->context, g, attacker->actor.id, 2, "FC Def", NULL);
          qa_lmctf_stat(f->context, attacker->actor.id, "defense-carrier", 1);
          qa_lmctf_print(g, NULL, "%s defends the %s flag carrier.\n", attacker_name, color);
        }
      }
    }
  }
  if (qa_lmctf_flags_carried(f, victim->actor.id, g) != NULL) {
    qa_lmctf_score(f->context, g, attacker->actor.id, 2, "FC Frag", NULL);
    qa_lmctf_stat(f->context, attacker->actor.id, "offense-carrier", 1);
    qa_lmctf_print(g, NULL, "%s killed the enemy flag carrier.\n", attacker_name);
    source->kill_carrier_time = now;
  }
}
