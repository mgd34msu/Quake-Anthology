/* Port of src/content/q2/multiplayer/ctf/match.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/match.h"
#include "../multiplayer_internal.h"
#include <ctype.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js_case.h"

struct qa_q2_ctf_match {
  qa_q2_ctf_context *context;
  qa_q2_ctf_match_actions actions;
};

qa_q2_ctf_match *qa_q2_ctf_match_create(qa_q2_ctf_context *context, qa_q2_ctf_match_actions actions) {
  qa_q2_ctf_match *m = calloc(1, sizeof *m);
  if (m == NULL) return NULL;
  m->context = context;
  m->actions = actions;
  return m;
}
void qa_q2_ctf_match_destroy(qa_q2_ctf_match *m) { free(m); }

static qa_q2_ctf_match_state *ms(qa_q2_ctf_match *m) { return &m->context->match; }

void qa_q2_ctf_match_after_spawn(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  if (m->context->rules.competition > 1) {
    ms(m)->phase = "setup";
    double * ref = &ms(m)->match_time;
    *ref = qa_q2mp_now(g) + m->context->rules.setup_minutes * 60;
  }
}

void qa_q2_ctf_match_setup(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  ms(m)->phase = "setup";
  double * ref = &ms(m)->match_time;
  *ref = qa_q2mp_now(g) + m->context->rules.setup_minutes * 60;
  if (m->context->rules.competition < 3) m->context->rules.competition = 2;
  m->actions.reset_players(m->actions.ctx, g);
}

void qa_q2_ctf_match_assign_ghost(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_player_state *state = qa_ctf_player(m->context, entity->actor.id);
  if (state->team == 0) return;
  if (state->has_ghost_code) qa_q2_ctf_match_state_delete_ghost(ms(m), state->ghost_code);
  if ((double)ms(m)->ghost_count >= g->options.max_clients) {
    state->has_ghost_code = false;
    return;
  }
  double code = 10000 + floor(g->host->random(g->host->ctx) * 90000);
  while (qa_q2_ctf_match_state_ghost(ms(m), code) != NULL) code = 10000 + fmod(code - 9999, 90000);
  qa_q2_ctf_ghost ghost = {0};
  ghost.code = code;
  ghost.team = state->team;
  ghost.name = qa_q2mp_dup(qa_ctf_name(m->context, entity->actor.id));
  ghost.has_actor = true;
  ghost.actor = entity->actor.id;
  qa_q2_ctf_match_state_set_ghost(ms(m), ghost);
  state->has_ghost_code = true;
  state->ghost_code = code;
  char code_text[32];
  qa_q2mp_format_number(code_text, sizeof code_text, code);
  qa_ctf_print(g, &entity->actor.id, "chat", "Your ghost code is **** %s ****\n", code_text);
  qa_ctf_print(g, &entity->actor.id, NULL, "If you lose connection, rejoin with your score intact by typing \"ghost %s\".\n", code_text);
}

void qa_q2_ctf_match_sync_ghost(qa_q2_ctf_match *m, qa_actor_id actor) {
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(m->context, actor);
  qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, actor);
  qa_q2_ctf_ghost *ghost = state != NULL && state->has_ghost_code ? qa_q2_ctf_match_state_ghost(ms(m), state->ghost_code) : NULL;
  if (ghost != NULL && player != NULL) {
    ghost->score = player->score;
    free(ghost->name);
    ghost->name = qa_q2mp_dup(player->name);
  }
}

bool qa_q2_ctf_match_restore_ghost(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, double code) {
  qa_q2_ctf_player_state *state = qa_ctf_player(m->context, entity->actor.id);
  qa_q2_ctf_ghost *ghost = qa_q2_ctf_match_state_ghost(ms(m), code);
  qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, entity->actor.id);
  if (state->team != 0 || strcmp(ms(m)->phase, "game") != 0 || ghost == NULL || player == NULL) return false;
  qa_q2_ctf_player_state *old = ghost->has_actor ? qa_q2_ctf_context_get(m->context, ghost->actor) : NULL;
  if (old != NULL) old->has_ghost_code = false;
  state->has_ghost_code = true;
  state->ghost_code = code;
  ghost->has_actor = true;
  ghost->actor = entity->actor.id;
  state->spawn_state = 0;
  qa_q2_ctf_playing_team team = ghost->team;
  double score = ghost->score;
  m->actions.join(m->actions.ctx, entity, g, team, true);
  player->score = score;
  qa_ctf_print(g, NULL, NULL, "%s has been reinstated to %s team.\n", player->name, qa_ctf_team_name(team));
  return true;
}

bool qa_q2_ctf_match_ready(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, bool ready) {
  qa_q2_ctf_player_state *state = qa_ctf_player(m->context, entity->actor.id);
  const char *phase = ms(m)->phase;
  if (state->team == 0 || (ready && strcmp(phase, "setup") != 0) ||
      (!ready && strcmp(phase, "setup") != 0 && strcmp(phase, "pregame") != 0) || state->ready == ready) return false;
  state->ready = ready;
  qa_ctf_print(g, NULL, NULL, "%s is %s.\n", qa_ctf_name(m->context, entity->actor.id), ready ? "ready" : "no longer ready");
  if (!ready && strcmp(phase, "pregame") == 0) {
    ms(m)->phase = "setup";
    double * ref2 = &ms(m)->match_time;
    *ref2 = qa_q2mp_now(g) + m->context->rules.setup_minutes * 60;
    qa_ctf_print(g, NULL, "chat", "Match halted.\n");
  }
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  bool all_ready = true, has_red = false, has_blue = false;
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(m->context, players[i]);
    if (member == NULL || member->team == 0) continue;
    if (!member->ready) all_ready = false;
    if (member->team == 1) has_red = true;
    if (member->team == 2) has_blue = true;
  }
  if (ready && all_ready && has_red && has_blue) {
    ms(m)->phase = "pregame";
    double * ref = &ms(m)->match_time;
    *ref = qa_q2mp_now(g) + m->context->rules.start_seconds;
    qa_ctf_print(g, NULL, "chat", "All players are ready. Match starting.\n");
  }
  return true;
}

void qa_q2_ctf_match_start(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  ms(m)->phase = "game";
  double * ref = &ms(m)->match_time;
  *ref = qa_q2mp_now(g) + m->context->rules.match_minutes * 60;
  ms(m)->team1 = 0;
  ms(m)->team2 = 0;
  qa_q2_ctf_match_state_clear_ghosts(ms(m));
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(m->context, players[i]);
    qa_q2_entity *entity = qa_q2_game_entity_of(g, &players[i]);
    qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, players[i]);
    if (state == NULL || entity == NULL || player == NULL) continue;
    player->score = 0;
    state->spawn_state = 0;
    state->has_ghost_code = false;
    state->has_last_returned_flag = false;
    state->has_last_fragged_carrier = false;
    state->has_last_hurt_carrier = false;
    qa_q2_presentation_event event = {0};
    event.kind = "centerprint";
    event.has_actor = true;
    event.actor = players[i];
    event.text = "******************\n\nMATCH HAS STARTED!\n\n******************";
    g->host->emit(g->host->ctx, event);
    if (state->team == 0) continue;
    qa_q2_ctf_match_assign_ghost(m, entity, g);
    m->actions.reset_grapple(m->actions.ctx, entity, g);
    m->context->hooks.observer(m->context->hooks.ctx, entity, g);
    player->dead = true;
    player->god = false;
    entity->visible = false;
    g->show(g->ctx, entity);
    state->has_match_respawn_at = true;
    double left = qa_q2mp_now(g) + 1;
    double right = floor(g->host->random(g->host->ctx) * 30) / 10;
    state->match_respawn_at = left + right;
    player->respawn_time = state->match_respawn_at;
  }
}

void qa_q2_ctf_match_totals(qa_q2_ctf_match *m, qa_q2_game_services *g, double out[2]) {
  double one = 0, two = 0;
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(m->context, players[i]);
    qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, players[i]);
    double score = player == NULL ? 0 : player->score;
    if (member != NULL && member->team == 1) one += score;
    else if (member != NULL && member->team == 2) two += score;
  }
  out[0] = one;
  out[1] = two;
}

void qa_q2_ctf_match_end(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  ms(m)->phase = "post";
  double totals[2];
  qa_q2_ctf_match_totals(m, g, totals);
  ms(m)->total1 = totals[0];
  ms(m)->total2 = totals[1];
  char t1[32], t2[32], tot1[32], tot2[32];
  qa_q2mp_format_number(t1, sizeof t1, ms(m)->team1);
  qa_q2mp_format_number(tot1, sizeof tot1, ms(m)->total1);
  qa_q2mp_format_number(t2, sizeof t2, ms(m)->team2);
  qa_q2mp_format_number(tot2, sizeof tot2, ms(m)->total2);
  qa_ctf_print(g, NULL, "chat", "MATCH COMPLETED!\nRED TEAM: %s captures, %s points\nBLUE TEAM: %s captures, %s points\n", t1, tot1, t2, tot2);
  double captures = ms(m)->team1 - ms(m)->team2;
  double points = ms(m)->total1 - ms(m)->total2;
  double difference = captures != 0 ? captures : points;
  if (difference == 0) {
    qa_ctf_print(g, NULL, "chat", "TIE GAME!\n");
  } else {
    char diff[32];
    qa_q2mp_format_number(diff, sizeof diff, fabs(difference));
    qa_ctf_print(g, NULL, "chat", "%s team won by %s %s!\n", qa_ctf_team_name(difference > 0 ? 1 : 2), diff, captures != 0 ? "CAPTURES" : "POINTS");
  }
  m->context->hooks.end_level(m->context->hooks.ctx, g, NULL);
}

bool qa_q2_ctf_match_begin_election(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, const char *kind, const char *map) {
  if (map == NULL) map = "";
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  double count = 0;
  for (size_t i = 0; i < pn; i++)
    if (qa_q2_ctf_context_get(m->context, players[i]) != NULL) count++;
  double percentage = m->context->rules.election_percentage;
  if (ms(m)->election != NULL || percentage <= 0 || count < 2) return false;
  for (size_t i = 0; i < m->context->state_count; i++) m->context->states[i].state.voted = false;
  char message[1024];
  if (strcmp(kind, "map") == 0)
    snprintf(message, sizeof message, "%s requested warping to %s.", qa_ctf_name(m->context, entity->actor.id), map);
  else if (strcmp(kind, "admin") == 0)
    snprintf(message, sizeof message, "%s requested admin rights.", qa_ctf_name(m->context, entity->actor.id));
  else
    snprintf(message, sizeof message, "%s requested match mode.", qa_ctf_name(m->context, entity->actor.id));
  qa_q2_ctf_election *election = qa_q2mp_xalloc(calloc(1, sizeof *election));
  election->kind = kind;
  election->target = entity->actor.id;
  election->map = qa_q2mp_dup(map);
  election->message = qa_q2mp_dup(message);
  election->votes = 0;
  double needed = trunc(count * percentage / 100);
  election->needed = needed < 1 ? 1 : needed;
  election->expires = qa_q2mp_now(g) + 20;
  qa_q2_ctf_match_state_set_election(ms(m), election);
  qa_ctf_print(g, NULL, "chat", "%s\nType YES or NO to vote on this request.\n", message);
  return true;
}

bool qa_q2_ctf_match_vote(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, bool yes) {
  qa_q2_ctf_player_state *state = qa_ctf_player(m->context, entity->actor.id);
  qa_q2_ctf_election *election = ms(m)->election;
  if (election == NULL || state->voted || qa_same_actor(election->target, entity->actor.id) || qa_q2mp_now(g) >= election->expires) return false;
  state->voted = true;
  if (yes) election->votes++;
  if (election->votes >= election->needed) {
    const char *kind = election->kind;
    char *map = qa_q2mp_dup(election->map);
    qa_actor_id target = election->target;
    qa_q2_ctf_match_state_set_election(ms(m), NULL);
    if (strcmp(kind, "match") == 0) qa_q2_ctf_match_setup(m, g);
    else if (strcmp(kind, "map") == 0) m->context->hooks.end_level(m->context->hooks.ctx, g, map);
    else {
      qa_q2_ctf_player_state *target_state = qa_q2_ctf_context_get(m->context, target);
      if (target_state != NULL) {
        target_state->admin = true;
        qa_ctf_print(g, NULL, NULL, "%s has become an admin.\n", qa_ctf_name(m->context, target));
      }
    }
    free(map);
  } else {
    double left = trunc(election->expires - qa_q2mp_now(g));
    if (left < 0) left = 0;
    char votes[32], needed[32], left_text[32];
    qa_q2mp_format_number(votes, sizeof votes, election->votes);
    qa_q2mp_format_number(needed, sizeof needed, election->needed);
    qa_q2mp_format_number(left_text, sizeof left_text, left);
    qa_ctf_print(g, NULL, NULL, "Votes: %s Needed: %s Time left: %ss\n", votes, needed, left_text);
  }
  return true;
}

bool qa_q2_ctf_match_check_rules(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  double now = qa_q2mp_now(g);
  if (ms(m)->election != NULL && ms(m)->election->expires <= now) {
    qa_q2_ctf_match_state_set_election(ms(m), NULL);
    qa_ctf_print(g, NULL, "chat", "Election timed out and has been cancelled.\n");
  }
  if (strcmp(ms(m)->phase, "none") == 0) {
    double max_team = ms(m)->team1 > ms(m)->team2 ? ms(m)->team1 : ms(m)->team2;
    if (m->context->rules.capture_limit > 0 && max_team >= m->context->rules.capture_limit) {
      qa_ctf_print(g, NULL, NULL, "Capturelimit hit.\n");
      return true;
    }
    return false;
  }
  if (ms(m)->match_time <= now) {
    if (strcmp(ms(m)->phase, "setup") == 0) {
      if (m->context->rules.competition < 3) {
        ms(m)->phase = "none";
        m->context->rules.competition = 1;
        m->actions.reset_players(m->actions.ctx, g);
      } else {
        ms(m)->match_time = now + m->context->rules.setup_minutes * 60;
      }
    } else if (strcmp(ms(m)->phase, "pregame") == 0) {
      qa_q2_ctf_match_start(m, g);
    } else if (strcmp(ms(m)->phase, "game") == 0) {
      qa_q2_ctf_match_end(m, g);
    }
  }
  double remaining = trunc(ms(m)->match_time - now);
  if (remaining < 0) remaining = 0;
  if (remaining != ms(m)->last_time) {
    ms(m)->last_time = remaining;
    char *status = qa_q2_ctf_match_status(m, g);
    qa_q2_ctf_event event = {0};
    event.kind = "match-status";
    event.text = status;
    m->context->hooks.emit(m->context->hooks.ctx, event);
    free(status);
  }
  return false;
}

char *qa_q2_ctf_match_status(qa_q2_ctf_match *m, qa_q2_game_services *g) {
  /* Math.max(0, Math.trunc(...)): trunc(-0.3) is -0, which "%02.0f" printed as "-0". */
  double left = ms(m)->match_time;
  double right = qa_q2mp_now(g);
  double time = qa_js_max(0, trunc(left - right));
  char minutes[48], seconds[48], clock[100];
  qa_net_js_number(floor(time / 60), minutes, sizeof minutes);
  qa_net_js_number(fmod(time, 60), seconds, sizeof seconds);
  /* String(...).padStart(2, "0") */
  snprintf(clock, sizeof clock, "%s%s:%s%s", strlen(minutes) < 2 ? "0" : "", minutes,
           strlen(seconds) < 2 ? "0" : "", seconds);
  char buffer[256];
  if (strcmp(ms(m)->phase, "setup") == 0) {
    size_t pn = 0;
    const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
    double waiting = 0;
    for (size_t i = 0; i < pn; i++) {
      qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(m->context, players[i]);
      if (member != NULL && member->team != 0 && !member->ready) waiting++;
    }
    char waiting_text[32];
    qa_q2mp_format_number(waiting_text, sizeof waiting_text, waiting);
    if (m->context->rules.competition < 3) snprintf(buffer, sizeof buffer, "%s SETUP: %s not ready", clock, waiting_text);
    else snprintf(buffer, sizeof buffer, "SETUP: %s not ready", waiting_text);
  } else if (strcmp(ms(m)->phase, "pregame") == 0) {
    snprintf(buffer, sizeof buffer, "%s UNTIL START", clock);
  } else if (strcmp(ms(m)->phase, "game") == 0) {
    snprintf(buffer, sizeof buffer, "%s MATCH", clock);
  } else {
    buffer[0] = '\0';
  }
  return qa_q2mp_dup(buffer);
}

bool qa_q2_ctf_match_admin(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, const char *password) {
  qa_q2_ctf_player_state *state = qa_ctf_player(m->context, entity->actor.id);
  if (!state->admin && password[0] != '\0' && m->context->rules.admin_password[0] != '\0' &&
      strcmp(password, m->context->rules.admin_password) == 0) {
    state->admin = true;
    qa_ctf_print(g, NULL, NULL, "%s has become an admin.\n", qa_ctf_name(m->context, entity->actor.id));
  }
  if (!state->admin) return qa_q2_ctf_match_begin_election(m, entity, g, "admin", "");
  qa_q2_ctf_menu_entry entries[3];
  char label1[] = "Settings";
  char *label2 = qa_q2mp_dup(strcmp(ms(m)->phase, "setup") == 0 ? "Force start match" : "Switch to match setup");
  char label3[] = "Cancel";
  entries[0] = (qa_q2_ctf_menu_entry){label1, "admin-settings"};
  entries[1] = (qa_q2_ctf_menu_entry){label2, "admin-start"};
  entries[2] = (qa_q2_ctf_menu_entry){label3, "close"};
  qa_q2_ctf_event event = {0};
  event.kind = "menu";
  event.actor = entity->actor.id;
  event.title = "Administration Menu";
  event.entries = entries;
  event.entry_count = 3;
  m->context->hooks.emit(m->context->hooks.ctx, event);
  free(label2);
  return true;
}

bool qa_q2_ctf_match_configure(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, qa_q2_ctf_admin_settings values) {
  if (!qa_ctf_player(m->context, entity->actor.id)->admin) return false;
  double durations[3] = {values.match_minutes, values.setup_minutes, values.start_seconds};
  for (int i = 0; i < 3; i++)
    if (!isfinite(durations[i]) || durations[i] <= 0) qa_range_error("CTF match durations must be positive");
  qa_q2_ctf_rules *rules = &m->context->rules;
  if (strcmp(ms(m)->phase, "game") == 0) ms(m)->match_time += (values.match_minutes - rules->match_minutes) * 60;
  else if (strcmp(ms(m)->phase, "setup") == 0) ms(m)->match_time += (values.setup_minutes - rules->setup_minutes) * 60;
  else if (strcmp(ms(m)->phase, "pregame") == 0) ms(m)->match_time += values.start_seconds - rules->start_seconds;
  rules->match_minutes = values.match_minutes;
  rules->setup_minutes = values.setup_minutes;
  rules->start_seconds = values.start_seconds;
  rules->instant_weapons = values.instant_weapons;
  rules->match_lock = values.match_lock;
  int64_t flags = (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & ~(4 | 16 | 16384)) |
                  (values.weapons_stay ? 4 : 0) | (values.instant_items ? 16 : 0) | (values.quad_drop ? 16384 : 0);
  m->context->hooks.set_deathmatch_flags(m->context->hooks.ctx, (double)flags);
  qa_ctf_print(g, NULL, NULL, "%s changed match settings.\n", qa_ctf_name(m->context, entity->actor.id));
  return true;
}

void qa_q2_ctf_match_settings_menu(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (!qa_ctf_player(m->context, entity->actor.id)->admin) return;
  qa_q2_ctf_rules *rules = &m->context->rules;
  int32_t flags = qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)); /* TS dmflags & x: ToInt32 */
  qa_q2_ctf_event event = {0};
  event.kind = "admin-settings";
  event.actor = entity->actor.id;
  event.settings = (qa_q2_ctf_admin_settings){rules->match_minutes, rules->setup_minutes, rules->start_seconds,
      (flags & 4) != 0, (flags & 16) != 0, (flags & 16384) != 0, rules->instant_weapons, rules->match_lock};
  m->context->hooks.emit(m->context->hooks.ctx, event);
}

static bool eq_ci(const char *a, const char *b) {
  return qa_js_equals_ignore_case(a, b); /* TS toLowerCase() === toLowerCase() */
}

bool qa_q2_ctf_match_warp(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, const char *requested) {
  const char *map = NULL;
  for (size_t i = 0; i < m->context->rules.warp_count; i++)
    if (eq_ci(m->context->rules.warp_list[i], requested)) { map = m->context->rules.warp_list[i]; break; }
  if (map == NULL) {
    char list[1024];
    list[0] = '\0';
    for (size_t i = 0; i < m->context->rules.warp_count; i++) {
      if (i > 0) strncat(list, " ", sizeof list - strlen(list) - 1);
      strncat(list, m->context->rules.warp_list[i], sizeof list - strlen(list) - 1);
    }
    qa_ctf_print(g, &entity->actor.id, NULL, "Available levels: %s\n", list);
    return false;
  }
  if (qa_ctf_player(m->context, entity->actor.id)->admin) {
    m->context->hooks.end_level(m->context->hooks.ctx, g, map);
    return true;
  }
  return qa_q2_ctf_match_begin_election(m, entity, g, "map", map);
}

bool qa_q2_ctf_match_boot(qa_q2_ctf_match *m, qa_q2_entity *entity, qa_q2_game_services *g, const char *number) {
  if (!qa_ctf_player(m->context, entity->actor.id)->admin) return false;
  if (number[0] == '\0') return false;
  for (const char *p = number; *p; p++)
    if (!isdigit((unsigned char)*p)) return false;
  double slot = qa_js_string_to_number(number) - 1;
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, players[i]);
    if (player != NULL && player->slot == slot) {
      m->context->hooks.kick(m->context->hooks.ctx, players[i]);
      return true;
    }
  }
  return false;
}
