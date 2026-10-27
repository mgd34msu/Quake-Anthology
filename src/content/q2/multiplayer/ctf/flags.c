/* Port of src/content/q2/multiplayer/ctf/flags.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/flags.h"
#include "../multiplayer_internal.h"

struct qa_q2_ctf_flags {
  qa_q2_ctf_context *context;
  qa_q2_named_think thinks[3];
  qa_q2_named_touch touches[2];
  qa_q2_callback_definitions callbacks;
};

bool qa_ctf_flag_team(const char *classname, qa_q2_ctf_playing_team *out) {
  if (strcmp(classname, QA_CTF_FLAGS[1].classname) == 0) { *out = 1; return true; }
  if (strcmp(classname, QA_CTF_FLAGS[2].classname) == 0) { *out = 2; return true; }
  return false;
}

bool qa_ctf_can_see(qa_q2_entity *target, qa_q2_entity *viewer, qa_q2_game_services *g) {
  if (strcmp(target->motion, "push") == 0) return false;
  qa_body_state body = qa_q2_game_body(g, target);
  qa_vec3 eye = qa_q2_add(qa_q2_game_body(g, viewer).origin, (qa_vec3){0, 0, viewer->view_height});
  double xs[2] = {body.bounds.min.x, body.bounds.max.x};
  double ys[2] = {body.bounds.min.y, body.bounds.max.y};
  double zs[2] = {body.bounds.min.z, body.bounds.max.z};
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      for (int k = 0; k < 2; k++)
        if (qa_q2mp_trace(g, eye, qa_q2_add(body.origin, (qa_vec3){xs[i], ys[j], zs[k]}), NULL, &viewer->actor.id, 3).fraction == 1) return true;
  return false;
}

static void pickup(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor);
static void targets(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor);
static void flag_sound(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, const char *path);

static void animate_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g);
void qa_q2_ctf_flags_touch_impl(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void setup_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_flags *f = ctx;
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_vec3 origin = qa_q2_game_body(g, entity).origin;
  qa_trace_result trace = qa_q2mp_trace(g, origin, qa_q2_add(origin, (qa_vec3){0, 0, -128}), &bounds, &entity->actor.id, 3);
  if (trace.start_solid) {
    char msg[256];
    snprintf(msg, sizeof msg, "CTFFlagSetup: %s starts solid", entity->classname);
    g->host->diagnostic(g->host->ctx, msg);
    g->remove(g->ctx, entity);
    return;
  }
  qa_q2_body_changes move = {0};
  move.has_bounds = true; move.bounds = bounds;
  move.has_origin = true; move.origin = trace.end;
  qa_q2_game_move(g, entity, move, true);
  entity->touch = (qa_q2_touch){f, qa_q2_ctf_flags_touch_impl};
  g->solid(g->ctx, entity, "trigger");
  g->motion(g->ctx, entity, "toss");
  g->show(g->ctx, entity);
  qa_q2_think animate = {f, animate_fn};
  g->schedule(g->ctx, entity, 0.1, animate);
}

static void animate_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (strcmp(entity->solid, "none") != 0) {
    double v = fmod(entity->frame - 173 + 1, 16);
    entity->frame = 173 + fmod(v + 16, 16);
  }
  g->show(g->ctx, entity);
  qa_q2_think self = {ctx, animate_fn};
  g->schedule(g->ctx, entity, 0.1, self);
}

void qa_q2_ctf_flags_touch_impl(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  pickup(ctx, entity, g, contact.other);
}
static void drop_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  if (entity->has_owner && qa_same_actor(entity->owner, contact.other) && qa_q2mp_now(g) < entity->timestamp + 2) return;
  pickup(ctx, entity, g, contact.other);
}
static void return_dropped_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_playing_team team;
  if (qa_ctf_flag_team(entity->classname, &team)) {
    qa_q2_ctf_flags_reset(ctx, g, team);
    qa_ctf_print(g, NULL, NULL, "The %s flag has returned!\n", qa_ctf_team_name(team));
  }
}

qa_q2_ctf_flags *qa_q2_ctf_flags_create(qa_q2_ctf_context *context) {
  qa_q2_ctf_flags *f = calloc(1, sizeof *f);
  if (f == NULL) return NULL;
  f->context = context;
  f->thinks[0] = (qa_q2_named_think){"CTFFlagThink", {f, animate_fn}};
  f->thinks[1] = (qa_q2_named_think){"CTFFlagSetup", {f, setup_fn}};
  f->thinks[2] = (qa_q2_named_think){"CTFDropFlagThink", {f, return_dropped_fn}};
  f->touches[0] = (qa_q2_named_touch){"CTFDropFlagTouch", {f, drop_touch_fn}};
  f->touches[1] = (qa_q2_named_touch){"CTF_FlagTouch", {f, qa_q2_ctf_flags_touch_impl}};
  f->callbacks.think = f->thinks; f->callbacks.think_count = 3;
  f->callbacks.touch = f->touches; f->callbacks.touch_count = 2;
  return f;
}
void qa_q2_ctf_flags_destroy(qa_q2_ctf_flags *f) { free(f); }
qa_q2_callback_definitions qa_q2_ctf_flags_callbacks(qa_q2_ctf_flags *f) { return f->callbacks; }

static bool item_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player) {
  pickup(ctx, entity, g, player.id);
  return false;
}

void qa_q2_ctf_flags_register(qa_q2_ctf_flags *f) {
  for (int team = 1; team <= 2; team++) {
    const qa_q2_ctf_flag_def *flag = &QA_CTF_FLAGS[team];
    qa_q2_item_definition def = {0};
    def.kind = "custom";
    def.console_give = "individual-only";
    def.classname = flag->classname;
    def.model = flag->model;
    def.icon = flag->icon;
    def.name = team == 1 ? "Red Flag" : "Blue Flag";
    def.sound = "ctf/flagtk.wav";
    def.rotate = false;
    def.respawn = 0;
    def.capacity = 1;
    def.quantity = 1;
    def.has_coop_stay = true;
    def.coop_stay = false;
    def.droppable = false;
    def.has_use = false;
    def.custom_ctx = f;
    def.pickup = item_pickup;
    qa_q2_item_module_register(f->context->hooks.items, def);
  }
}

bool qa_q2_ctf_flags_spawn(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_playing_team team;
  if (!qa_ctf_flag_team(entity->classname, &team)) return false;
  if (entity->model == NULL || entity->model[0] == '\0') entity->model = QA_CTF_FLAGS[(int)team].model;
  entity->effects = (double)((int64_t)entity->effects | (int64_t)QA_CTF_FLAGS[(int)team].effect);
  entity->render_flags = (double)((int64_t)entity->render_flags | 512);
  entity->frame = 173;
  qa_q2_think setup = {f, setup_fn};
  g->schedule(g->ctx, entity, g->host->frame_seconds(g->host->ctx) * 2, setup);
  return true;
}

qa_q2_entity *qa_q2_ctf_flags_base(qa_q2_ctf_flags *f, qa_q2_game_services *g, qa_q2_ctf_playing_team team) {
  (void)f;
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++)
    if (strcmp(all[i]->classname, QA_CTF_FLAGS[(int)team].classname) == 0 && ((int64_t)all[i]->spawnflags & 0x30000) == 0) return all[i];
  return NULL;
}

const char *qa_q2_ctf_flags_state(qa_q2_ctf_flags *f, qa_q2_game_services *g, qa_q2_ctf_playing_team team) {
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++)
    if (strcmp(all[i]->classname, QA_CTF_FLAGS[(int)team].classname) == 0 && ((int64_t)all[i]->spawnflags & 0x30000) != 0) return "dropped";
  qa_q2_entity *base = qa_q2_ctf_flags_base(f, g, team);
  return base != NULL && strcmp(base->solid, "trigger") == 0 ? "base" : "taken";
}

void qa_q2_ctf_flags_reset(qa_q2_ctf_flags *f, qa_q2_game_services *g, qa_q2_ctf_playing_team team) {
  (void)f;
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  /* Snapshot: remove may mutate the services list. */
  qa_q2_entity **copy = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *copy));
  for (size_t i = 0; i < n; i++) copy[i] = all[i];
  for (size_t i = 0; i < n; i++) {
    qa_q2_entity *entity = copy[i];
    if (strcmp(entity->classname, QA_CTF_FLAGS[(int)team].classname) != 0) continue;
    if (((int64_t)entity->spawnflags & 0x30000) != 0) {
      g->remove(g->ctx, entity);
    } else {
      entity->visible = true;
      entity->server_flags = (double)((int64_t)entity->server_flags & ~1);
      g->solid(g->ctx, entity, "trigger");
      g->show(g->ctx, entity);
      qa_q2_presentation_event event = {0};
      event.kind = "entity-event";
      event.has_actor = true;
      event.actor = entity->actor.id;
      event.event = 1;
      g->host->emit(g->host->ctx, event);
    }
  }
  free(copy);
}

void qa_q2_ctf_flags_reset_all(qa_q2_ctf_flags *f, qa_q2_game_services *g) {
  qa_q2_ctf_flags_reset(f, g, 1);
  qa_q2_ctf_flags_reset(f, g, 2);
}

static void pickup(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor) {
  qa_q2_ctf_context *context = f->context;
  if (strcmp(context->match.phase, "setup") == 0 || strcmp(context->match.phase, "pregame") == 0) return;
  qa_q2_ctf_playing_team team;
  bool has_team = qa_ctf_flag_team(entity->classname, &team);
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(context, actor);
  qa_q2_entity *player = qa_q2_game_entity_of(g, &actor);
  qa_q2mp_player_state *common = context->hooks.player(context->hooks.ctx, actor);
  if (!has_team || state == NULL || state->team == 0 || player == NULL || common == NULL || common->spectator ||
      qa_q2mp_health_or(g, actor, 0) <= 0) return;
  if (strcmp(entity->solid, "trigger") != 0) return;
  double now = qa_q2mp_now(g);
  qa_q2_ctf_playing_team enemy = qa_other_ctf_team(team);
  const qa_q2_ctf_flag_def *flag = &QA_CTF_FLAGS[(int)team];
  bool dropped = ((int64_t)entity->spawnflags & 0x30000) != 0;
  qa_shared_inventory_table *inv = g->host->inventory;
  if (state->team == team) {
    if (dropped) {
      qa_ctf_score(context, actor, 1);
      state->has_last_returned_flag = true;
      state->last_returned_flag = now;
      qa_ctf_print(g, NULL, NULL, "%s returned the %s flag!\n", qa_ctf_name(context, actor), qa_ctf_team_name(team));
      flag_sound(f, entity, g, "ctf/flagret.wav");
      qa_q2_ctf_flags_reset(f, g, team);
      return;
    }
    if (qa_q2mp_inv_count(g, actor, QA_CTF_FLAGS[(int)enemy].item) == 0) {
      targets(f, entity, g, actor);
      return;
    }
    qa_shared_inventory_table_consume(inv, player->actor, QA_CTF_FLAGS[(int)enemy].item, 1);
    qa_q2_ctf_match_state *match = &context->match;
    if (team == 1) match->team1++; else match->team2++;
    match->has_last_flag_capture = true;
    match->last_flag_capture = now;
    match->has_last_capture_team = true;
    match->last_capture_team = team;
    qa_ctf_score(context, actor, 15);
    qa_q2_ctf_ghost *ghost = state->has_ghost_code ? qa_q2_ctf_match_state_ghost(match, state->ghost_code) : NULL;
    if (ghost != NULL) ghost->captures++;
    qa_ctf_print(g, NULL, NULL, "%s captured the %s flag!\n", qa_ctf_name(context, actor), qa_ctf_team_name(enemy));
    flag_sound(f, entity, g, "ctf/flagcap.wav");
    size_t pn = 0;
    const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
    for (size_t i = 0; i < pn; i++) {
      qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(context, players[i]);
      if (member == NULL) continue;
      if (member->team != team) {
        member->has_last_hurt_carrier = false;
        continue;
      }
      if (!qa_same_actor(players[i], actor)) qa_ctf_score(context, players[i], 10);
      if (member->has_last_returned_flag && member->last_returned_flag + 10 > now) {
        qa_ctf_score(context, players[i], 1);
        qa_ctf_print(g, NULL, NULL, "%s gets an assist for returning the flag!\n", qa_ctf_name(context, players[i]));
      }
      if (member->has_last_fragged_carrier && member->last_fragged_carrier + 10 > now) {
        qa_ctf_score(context, players[i], 2);
        qa_ctf_print(g, NULL, NULL, "%s gets an assist for fragging the flag carrier!\n", qa_ctf_name(context, players[i]));
      }
    }
    qa_q2_ctf_flags_reset_all(f, g);
    targets(f, entity, g, actor);
    return;
  }
  if (qa_q2mp_inv_count(g, actor, flag->item) != 0) return;
  size_t en = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(inv, actor, &en);
  bool has_entry = false;
  for (size_t i = 0; i < en; i++)
    if (entries[i].item != NULL && strcmp(entries[i].item, flag->item) == 0) has_entry = true;
  if (!has_entry) {
    qa_inventory_entry entry = {0};
    entry.item = flag->item; entry.count = 0; entry.capacity = 1;
    qa_shared_inventory_table_configure(inv, player->actor, entry, NULL, NULL);
  }
  qa_shared_inventory_table_give(inv, player->actor, flag->item, 1);
  state->flag_since = now;
  qa_ctf_print(g, NULL, NULL, "%s got the %s flag!\n", qa_ctf_name(context, actor), qa_ctf_team_name(team));
  qa_q2_presentation_event event = {0};
  event.kind = "pickup";
  event.player = actor;
  event.item = flag->item;
  event.name = team == 1 ? "Red Flag" : "Blue Flag";
  event.icon = flag->icon;
  g->host->emit(g->host->ctx, event);
  g->sound(g->ctx, player->actor, "ctf/flagtk.wav", 3, 1, 1);
  targets(f, entity, g, actor);
  if (!qa_session_actor_registry_is_live(g->host->actors, entity->actor.id)) return;
  if (dropped) {
    g->remove(g->ctx, entity);
    return;
  }
  entity->visible = false;
  entity->server_flags = (double)((int64_t)entity->server_flags | 1);
  g->solid(g->ctx, entity, "none");
  g->show(g->ctx, entity);
}

static void targets(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor) {
  (void)f;
  if (((int64_t)entity->spawnflags & 0x40000) == 0) {
    entity->spawnflags = (double)((int64_t)entity->spawnflags | 0x40000);
    qa_q2_game_use_targets_entity(g, entity, &actor, false);
  }
}

static void flag_sound(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g, const char *path) {
  (void)f;
  qa_q2_presentation_event event = {0};
  event.kind = "sound";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.origin = qa_q2_game_body(g, entity).origin;
  event.path = path;
  event.channel = 2;
  event.volume = 1;
  event.attenuation = 0;
  event.reliable = true;
  event.loop = "once";
  g->host->emit(g->host->ctx, event);
}

qa_q2_entity *qa_q2_ctf_flags_drop(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_playing_team team;
  if (!qa_ctf_carried_flag(g, entity->actor.id, &team)) return NULL;
  const qa_q2_ctf_flag_def *flag = &QA_CTF_FLAGS[(int)team];
  qa_q2_entity *dropped = g->create(g->ctx, flag->classname, NULL, 0);
  qa_body_state body = qa_q2_game_body(g, entity);
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, entity->actor.id);
  qa_vec3 forward = qa_q2_movedir(view.has_value ? view.value.view_angles : body.angles);
  dropped->model = flag->model;
  dropped->effects = flag->effect;
  dropped->render_flags = 512;
  dropped->spawnflags = 0x10000;
  dropped->has_owner = true;
  dropped->owner = entity->actor.id;
  dropped->timestamp = qa_q2mp_now(g);
  dropped->touch = (qa_q2_touch){f, drop_touch_fn};
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_vec3 end = qa_q2_add(qa_q2_add(body.origin, qa_q2_scale(forward, 24)), (qa_vec3){0, 0, -16});
  qa_vec3 origin = qa_q2mp_trace(g, body.origin, end, &bounds, &entity->actor.id, 3).end;
  qa_vec3 velocity = qa_q2_scale(forward, 100);
  velocity.z = 300;
  qa_q2_body_changes move = {0};
  move.has_origin = true; move.origin = origin;
  move.has_bounds = true; move.bounds = bounds;
  move.has_velocity = true; move.velocity = velocity;
  qa_q2_game_move(g, dropped, move, true);
  g->solid(g->ctx, dropped, "trigger");
  g->motion(g->ctx, dropped, "toss");
  g->show(g->ctx, dropped);
  qa_q2_think ret = {f, return_dropped_fn};
  g->schedule(g->ctx, dropped, 30, ret);
  qa_shared_inventory_table_consume(g->host->inventory, entity->actor, flag->item, 1);
  qa_ctf_print(g, NULL, NULL, "%s lost the %s flag!\n", qa_ctf_name(f->context, entity->actor.id), qa_ctf_team_name(team));
  return dropped;
}

void qa_q2_ctf_flags_hurt_carrier(qa_q2_ctf_flags *f, qa_actor_id target, const qa_actor_id *attacker, qa_q2_game_services *g) {
  qa_q2_ctf_player_state *victim = qa_q2_ctf_context_get(f->context, target);
  qa_q2_ctf_player_state *aggressor = attacker == NULL ? NULL : qa_q2_ctf_context_get(f->context, *attacker);
  qa_q2_ctf_playing_team carried;
  if (victim != NULL && aggressor != NULL && victim->team != 0 && aggressor->team != 0 && victim->team != aggressor->team &&
      qa_ctf_carried_flag(g, target, &carried) && carried == qa_other_ctf_team((qa_q2_ctf_playing_team)victim->team)) {
    aggressor->has_last_hurt_carrier = true;
    aggressor->last_hurt_carrier = qa_q2mp_now(g);
  }
}

static bool near_object(qa_q2_ctf_flags *f, qa_q2_entity *object, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g) {
  (void)f;
  qa_vec3 o = qa_q2_game_body(g, object).origin;
  return qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, victim).origin, o)) < 400 ||
         qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, attacker).origin, o)) < 400 ||
         qa_ctf_can_see(object, victim, g) || qa_ctf_can_see(object, attacker, g);
}

void qa_q2_ctf_flags_frag(qa_q2_ctf_flags *f, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g) {
  qa_q2_ctf_context *context = f->context;
  qa_q2_ctf_player_state *target = qa_q2_ctf_context_get(context, victim->actor.id);
  qa_q2_ctf_player_state *source = attacker == NULL ? NULL : qa_q2_ctf_context_get(context, attacker->actor.id);
  qa_q2_ctf_ghost *victim_ghost = target != NULL && target->has_ghost_code ? qa_q2_ctf_match_state_ghost(&context->match, target->ghost_code) : NULL;
  if (victim_ghost != NULL) victim_ghost->deaths++;
  if (attacker == NULL || source == NULL || target == NULL || attacker == victim) return;
  qa_q2_ctf_ghost *ghost = source->has_ghost_code ? qa_q2_ctf_match_state_ghost(&context->match, source->ghost_code) : NULL;
  if (ghost != NULL) ghost->kills++;
  if (target->team == 0 || source->team == 0 || target->team == source->team) return;
  double now = qa_q2mp_now(g);
  qa_q2_ctf_playing_team carried;
  if (qa_ctf_carried_flag(g, victim->actor.id, &carried) && carried == source->team) {
    source->has_last_fragged_carrier = true;
    source->last_fragged_carrier = now;
    qa_ctf_score(context, attacker->actor.id, 2);
    for (size_t i = 0; i < context->state_count; i++)
      if (context->states[i].state.team == source->team) context->states[i].state.has_last_hurt_carrier = false;
    qa_ctf_print(g, &attacker->actor.id, "medium", "BONUS: 2 points for fragging enemy flag carrier.\n");
    return;
  }
  if (target->has_last_hurt_carrier && now - target->last_hurt_carrier < 8 && !qa_ctf_carried_flag(g, attacker->actor.id, &carried)) {
    qa_ctf_score(context, attacker->actor.id, 2);
    if (ghost != NULL) ghost->carrier_defense++;
    qa_ctf_print(g, NULL, "medium", "%s defends %s's flag carrier against an aggressive enemy\n", qa_ctf_name(context, attacker->actor.id), qa_ctf_team_name(source->team));
    return;
  }
  qa_q2_entity *flag = qa_q2_ctf_flags_base(f, g, (qa_q2_ctf_playing_team)source->team);
  if (flag == NULL) return;
  if (near_object(f, flag, victim, attacker, g)) {
    qa_ctf_score(context, attacker->actor.id, 1);
    if (ghost != NULL) ghost->base_defense++;
    qa_ctf_print(g, NULL, "medium", "%s defends the %s %s.\n", qa_ctf_name(context, attacker->actor.id), qa_ctf_team_name(source->team), strcmp(flag->solid, "none") == 0 ? "base" : "flag");
    return;
  }
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  qa_q2_entity *carrier_entity = NULL;
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(context, players[i]);
    qa_q2_ctf_playing_team c;
    if (member != NULL && member->team == source->team && qa_ctf_carried_flag(g, players[i], &c) && c == target->team) {
      carrier_entity = qa_q2_game_entity_of(g, &players[i]);
      break;
    }
  }
  if (carrier_entity != NULL && carrier_entity != attacker && near_object(f, carrier_entity, victim, attacker, g)) {
    qa_ctf_score(context, attacker->actor.id, 1);
    if (ghost != NULL) ghost->carrier_defense++;
    qa_ctf_print(g, NULL, "medium", "%s defends %s's flag carrier.\n", qa_ctf_name(context, attacker->actor.id), qa_ctf_team_name(source->team));
  }
}

void qa_q2_ctf_flags_effects(qa_q2_ctf_flags *f, qa_q2_entity *entity, qa_q2_game_services *g) {
  (void)f;
  qa_q2_ctf_playing_team team;
  bool carried = qa_ctf_carried_flag(g, entity->actor.id, &team);
  entity->effects = (double)((int64_t)entity->effects & ~0xc0000);
  if (carried && qa_q2mp_health_or(g, entity->actor.id, 0) > 0)
    entity->effects = (double)((int64_t)entity->effects | (int64_t)QA_CTF_FLAGS[(int)team].effect);
  entity->model3 = carried ? QA_CTF_FLAGS[(int)team].model : "";
  g->show(g->ctx, entity);
}
