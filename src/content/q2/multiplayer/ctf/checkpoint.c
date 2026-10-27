/* Port of src/content/q2/multiplayer/ctf/checkpoint.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/checkpoint.h"
#include "../multiplayer_internal.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

void qa_q2_ctf_checkpoint_destroy(qa_q2_ctf_checkpoint *checkpoint) {
  for (size_t i = 0; i < checkpoint->match.ghost_count; i++) free(checkpoint->match.ghosts[i].name);
  free(checkpoint->match.ghosts);
  free(checkpoint->match.election.map);
  free(checkpoint->match.election.message);
  free(checkpoint->players);
  memset(checkpoint, 0, sizeof *checkpoint);
}

void qa_capture_q2_ctf(qa_q2_ctf_context *context, qa_q2_ctf_grapple_equipment *equipment, qa_q2_ctf_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  const qa_q2_ctf_rules *rules = &context->rules;
  out->rules = (qa_q2_ctf_rules_checkpoint){rules->force_join, rules->competition, rules->match_lock,
      rules->election_percentage, rules->match_minutes, rules->setup_minutes, rules->start_seconds,
      rules->capture_limit, rules->instant_weapons};
  const qa_q2_ctf_match_state *match = &context->match;
  qa_q2_ctf_match_checkpoint *mc = &out->match;
  mc->team1 = match->team1; mc->team2 = match->team2;
  mc->total1 = match->total1; mc->total2 = match->total2;
  mc->has_last_flag_capture = match->has_last_flag_capture;
  mc->last_flag_capture = match->last_flag_capture;
  mc->has_last_capture_team = match->has_last_capture_team;
  mc->last_capture_team = match->last_capture_team;
  mc->phase = match->phase;
  mc->match_time = match->match_time;
  mc->last_time = match->last_time;
  if (match->election != NULL) {
    mc->has_election = true;
    mc->election.kind = match->election->kind;
    mc->election.target = qa_save_ctf_actor(match->election->target);
    mc->election.map = qa_q2mp_dup(match->election->map);
    mc->election.message = qa_q2mp_dup(match->election->message);
    mc->election.votes = match->election->votes;
    mc->election.needed = match->election->needed;
    mc->election.expires = match->election->expires;
  }
  mc->ghost_count = match->ghost_count;
  mc->ghosts = qa_q2mp_xalloc(malloc((match->ghost_count == 0 ? 1 : match->ghost_count) * sizeof *mc->ghosts));
  for (size_t i = 0; i < match->ghost_count; i++) {
    const qa_q2_ctf_ghost *ghost = &match->ghosts[i];
    qa_q2_ctf_ghost_checkpoint *gc = &mc->ghosts[i];
    memset(gc, 0, sizeof *gc);
    gc->code = ghost->code;
    gc->team = ghost->team;
    gc->name = qa_q2mp_dup(ghost->name);
    gc->has_actor = ghost->has_actor;
    if (ghost->has_actor) gc->actor = qa_save_ctf_actor(ghost->actor);
    gc->score = ghost->score;
    gc->deaths = ghost->deaths;
    gc->kills = ghost->kills;
    gc->captures = ghost->captures;
    gc->base_defense = ghost->base_defense;
    gc->carrier_defense = ghost->carrier_defense;
  }
  out->player_count = context->state_count;
  out->players = qa_q2mp_xalloc(malloc((context->state_count == 0 ? 1 : context->state_count) * sizeof *out->players));
  for (size_t i = 0; i < context->state_count; i++) {
    qa_actor_id actor = context->states[i].actor;
    out->players[i].actor = qa_save_ctf_actor(actor);
    out->players[i].state.state = context->states[i].state;
    qa_ctf_grapple_state grapple;
    if (equipment != NULL) grapple = *qa_q2_ctf_grapple_state(equipment, actor);
    else qa_ctf_grapple_state_init(&grapple);
    out->players[i].state.grapple = qa_capture_ctf_grapple(grapple);
  }
}

void qa_restore_q2_ctf(qa_q2_ctf_context *context, qa_q2_game_services *g, const qa_q2_ctf_checkpoint *checkpoint, qa_q2_ctf_grapple_equipment *equipment) {
  if (equipment != NULL) qa_q2_ctf_grapple_bind(equipment, g);
  struct restored { qa_actor_id actor; qa_q2_ctf_player_state state; qa_ctf_grapple_state hook; };
  struct restored *players = qa_q2mp_xalloc(malloc((checkpoint->player_count == 0 ? 1 : checkpoint->player_count) * sizeof *players));
  for (size_t i = 0; i < checkpoint->player_count; i++) {
    const qa_q2_ctf_player_entry *entry = &checkpoint->players[i];
    qa_owned_actor owned;
    if (!qa_session_actor_registry_resolve_saved(g->host->actors, entry->actor, &owned) ||
        qa_q2_game_entity_of(g, &owned.id) == NULL || context->hooks.player(context->hooks.ctx, owned.id) == NULL) {
      free(players);
      qa_q2f_error("CTF restore requires the existing shared player and source entity");
    }
    players[i].actor = owned.id;
    qa_q2_ctf_player_state_init(&players[i].state);
    players[i].state = entry->state.state;
    players[i].hook = qa_restore_ctf_grapple(entry->state.grapple, g);
  }
  qa_q2_ctf_context_clear(context);
  /* TS also clears equipment.states; the landed equipment has no map
   * clear, so restored hooks below overwrite per-player state. */
  for (size_t i = 0; i < checkpoint->player_count; i++) {
    qa_q2_ctf_context_set(context, players[i].actor, players[i].state);
    if (equipment != NULL) *qa_q2_ctf_grapple_state(equipment, players[i].actor) = players[i].hook;
  }
  context->rules.force_join = checkpoint->rules.force_join;
  context->rules.competition = checkpoint->rules.competition;
  context->rules.match_lock = checkpoint->rules.match_lock;
  context->rules.election_percentage = checkpoint->rules.election_percentage;
  context->rules.match_minutes = checkpoint->rules.match_minutes;
  context->rules.setup_minutes = checkpoint->rules.setup_minutes;
  context->rules.start_seconds = checkpoint->rules.start_seconds;
  context->rules.capture_limit = checkpoint->rules.capture_limit;
  context->rules.instant_weapons = checkpoint->rules.instant_weapons;
  qa_q2_ctf_match_state *match = &context->match;
  const qa_q2_ctf_match_checkpoint *mc = &checkpoint->match;
  match->team1 = mc->team1; match->team2 = mc->team2;
  match->total1 = mc->total1; match->total2 = mc->total2;
  match->has_last_flag_capture = mc->has_last_flag_capture;
  match->last_flag_capture = mc->last_flag_capture;
  match->has_last_capture_team = mc->has_last_capture_team;
  match->last_capture_team = mc->last_capture_team;
  match->phase = mc->phase;
  match->match_time = mc->match_time;
  match->last_time = mc->last_time;
  qa_q2_ctf_match_state_clear_ghosts(match);
  for (size_t i = 0; i < mc->ghost_count; i++) {
    const qa_q2_ctf_ghost_checkpoint *gc = &mc->ghosts[i];
    qa_q2_ctf_ghost ghost = {0};
    ghost.code = gc->code;
    ghost.team = gc->team;
    ghost.name = qa_q2mp_dup(gc->name);
    ghost.has_actor = gc->has_actor;
    if (gc->has_actor) ghost.actor = qa_session_actor_registry_reference_saved(g->host->actors, gc->actor, "checkpoint");
    ghost.score = gc->score;
    ghost.deaths = gc->deaths;
    ghost.kills = gc->kills;
    ghost.captures = gc->captures;
    ghost.base_defense = gc->base_defense;
    ghost.carrier_defense = gc->carrier_defense;
    qa_q2_ctf_match_state_set_ghost(match, ghost);
  }
  if (mc->has_election) {
    qa_q2_ctf_election *election = qa_q2mp_xalloc(calloc(1, sizeof *election));
    election->kind = mc->election.kind;
    election->target = qa_session_actor_registry_reference_saved(g->host->actors, mc->election.target, "checkpoint");
    election->map = qa_q2mp_dup(mc->election.map);
    election->message = qa_q2mp_dup(mc->election.message);
    election->votes = mc->election.votes;
    election->needed = mc->election.needed;
    election->expires = mc->election.expires;
    qa_q2_ctf_match_state_set_election(match, election);
  } else {
    qa_q2_ctf_match_state_set_election(match, NULL);
  }
  if (g->options.edition == QA_Q2_EDITION_CLASSIC && equipment != NULL)
    for (size_t i = 0; i < checkpoint->player_count; i++) {
      qa_ctf_grapple_state *state = qa_q2_ctf_grapple_state(equipment, players[i].actor);
      context->hooks.set_grapple_prediction(context->hooks.ctx, players[i].actor,
          state->has_grapple && strcmp(state->grapple_state, "hang") == 0);
    }
  free(players);
}

/* ---- encode ---- */
static void set_field(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, qa_q2mp_xalloc(child))) qa_q2f_error("Out of memory");
}
static void set_num(qa_save_value *object, const char *key, double v) { set_field(object, key, qa_save_value_number(v)); }
static void set_str(qa_save_value *object, const char *key, const char *v) { set_field(object, key, qa_save_value_string(v)); }
static void set_bool(qa_save_value *object, const char *key, bool v) { set_field(object, key, qa_save_value_boolean(v)); }
static void set_null(qa_save_value *object, const char *key) { set_field(object, key, qa_save_value_null()); }
static qa_save_value *saved_actor_value(qa_saved_actor_id actor) {
  qa_save_value *value = qa_q2mp_xalloc(qa_save_value_object());
  set_num(value, "slot", actor.slot);
  set_num(value, "generation", actor.generation);
  return value;
}
static void set_nullable_num(qa_save_value *object, const char *key, bool has, double v) {
  if (has) set_num(object, key, v);
  else set_null(object, key);
}

uint8_t *qa_encode_q2_ctf_checkpoint(const qa_q2_ctf_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *root = qa_q2mp_xalloc(qa_save_value_object());
  set_num(root, "version", 1);
  qa_save_value *rules = qa_q2mp_xalloc(qa_save_value_object());
  set_str(rules, "forceJoin", checkpoint->rules.force_join);
  set_num(rules, "competition", checkpoint->rules.competition);
  set_bool(rules, "matchLock", checkpoint->rules.match_lock);
  set_num(rules, "electionPercentage", checkpoint->rules.election_percentage);
  set_num(rules, "matchMinutes", checkpoint->rules.match_minutes);
  set_num(rules, "setupMinutes", checkpoint->rules.setup_minutes);
  set_num(rules, "startSeconds", checkpoint->rules.start_seconds);
  set_num(rules, "captureLimit", checkpoint->rules.capture_limit);
  set_bool(rules, "instantWeapons", checkpoint->rules.instant_weapons);
  set_field(root, "rules", rules);
  qa_save_value *match = qa_q2mp_xalloc(qa_save_value_object());
  const qa_q2_ctf_match_checkpoint *mc = &checkpoint->match;
  set_num(match, "team1", mc->team1);
  set_num(match, "team2", mc->team2);
  set_num(match, "total1", mc->total1);
  set_num(match, "total2", mc->total2);
  set_nullable_num(match, "lastFlagCapture", mc->has_last_flag_capture, mc->last_flag_capture);
  set_nullable_num(match, "lastCaptureTeam", mc->has_last_capture_team, mc->last_capture_team);
  set_str(match, "phase", mc->phase);
  set_num(match, "matchTime", mc->match_time);
  set_num(match, "lastTime", mc->last_time);
  if (mc->has_election) {
    qa_save_value *election = qa_q2mp_xalloc(qa_save_value_object());
    set_str(election, "kind", mc->election.kind);
    set_field(election, "target", saved_actor_value(mc->election.target));
    set_str(election, "map", mc->election.map);
    set_str(election, "message", mc->election.message);
    set_num(election, "votes", mc->election.votes);
    set_num(election, "needed", mc->election.needed);
    set_num(election, "expires", mc->election.expires);
    set_field(match, "election", election);
  } else {
    set_null(match, "election");
  }
  qa_save_value *ghosts = qa_q2mp_xalloc(qa_save_value_array());
  for (size_t i = 0; i < mc->ghost_count; i++) {
    const qa_q2_ctf_ghost_checkpoint *gc = &mc->ghosts[i];
    qa_save_value *ghost = qa_q2mp_xalloc(qa_save_value_object());
    set_num(ghost, "code", gc->code);
    set_num(ghost, "team", gc->team);
    set_str(ghost, "name", gc->name);
    if (gc->has_actor) set_field(ghost, "actor", saved_actor_value(gc->actor));
    else set_null(ghost, "actor");
    set_num(ghost, "score", gc->score);
    set_num(ghost, "deaths", gc->deaths);
    set_num(ghost, "kills", gc->kills);
    set_num(ghost, "captures", gc->captures);
    set_num(ghost, "baseDefense", gc->base_defense);
    set_num(ghost, "carrierDefense", gc->carrier_defense);
    if (!qa_save_value_push(ghosts, ghost)) qa_q2f_error("Out of memory");
  }
  set_field(match, "ghosts", ghosts);
  set_field(root, "match", match);
  qa_save_value *players = qa_q2mp_xalloc(qa_save_value_array());
  for (size_t i = 0; i < checkpoint->player_count; i++) {
    const qa_q2_ctf_player_entry *entry = &checkpoint->players[i];
    const qa_q2_ctf_player_state *s = &entry->state.state;
    const qa_ctf_grapple_checkpoint *gr = &entry->state.grapple;
    qa_save_value *state = qa_q2mp_xalloc(qa_save_value_object());
    set_num(state, "team", s->team);
    set_num(state, "spawnState", s->spawn_state);
    set_nullable_num(state, "lastHurtCarrier", s->has_last_hurt_carrier, s->last_hurt_carrier);
    set_nullable_num(state, "lastReturnedFlag", s->has_last_returned_flag, s->last_returned_flag);
    set_nullable_num(state, "lastFraggedCarrier", s->has_last_fragged_carrier, s->last_fragged_carrier);
    set_num(state, "flagSince", s->flag_since);
    set_bool(state, "voted", s->voted);
    set_bool(state, "ready", s->ready);
    set_bool(state, "admin", s->admin);
    set_bool(state, "idView", s->id_view);
    set_nullable_num(state, "ghostCode", s->has_ghost_code, s->ghost_code);
    if (gr->has_grapple) set_field(state, "grapple", saved_actor_value(gr->grapple));
    else set_null(state, "grapple");
    set_str(state, "grappleState", gr->grapple_state);
    set_num(state, "grappleReleaseTime", gr->grapple_release_time);
    if (gr->has_no_knockback) set_bool(state, "grappleNoKnockback", gr->grapple_no_knockback);
    else set_null(state, "grappleNoKnockback");
    set_num(state, "regenTime", s->regen_time);
    set_num(state, "techSoundTime", s->tech_sound_time);
    set_num(state, "lastTechMessage", s->last_tech_message);
    set_nullable_num(state, "matchRespawnAt", s->has_match_respawn_at, s->match_respawn_at);
    qa_save_value *player = qa_q2mp_xalloc(qa_save_value_object());
    set_field(player, "actor", saved_actor_value(entry->actor));
    set_field(player, "state", state);
    if (!qa_save_value_push(players, player)) qa_q2f_error("Out of memory");
  }
  set_field(root, "players", players);
  uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return bytes;
}

/* ---- decode ---- */
static double read_field_finite(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  double value = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double read_field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  double value = qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double read_field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static bool read_field_bool(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static const char *read_field_choice(const qa_save_reader *reader, const char *name, const char *const *choices, size_t count) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  const char *value = qa_save_reader_choice_string(&field, choices, count);
  qa_save_reader_destroy(&field);
  /* Return the static choice literal, not tree-borrowed storage. */
  for (size_t i = 0; i < count; i++)
    if (strcmp(value, choices[i]) == 0) return choices[i];
  return value;
}
static const char *read_field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  const char *value = qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool read_nullable_finite(const qa_save_reader *reader, const char *name, double *out) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  bool has = !qa_save_reader_is_null(&field);
  if (has) *out = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  return has;
}
static bool read_nullable_integer_min(const qa_save_reader *reader, const char *name, double minimum, double *out) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  bool has = !qa_save_reader_is_null(&field);
  if (has) *out = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return has;
}
static qa_saved_actor_id read_field_actor(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  qa_saved_actor_id value = qa_read_saved_actor(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool read_nullable_actor(const qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_q2f_error("Out of memory");
  bool has = !qa_save_reader_is_null(&field);
  if (has) *out = qa_read_saved_actor(&field);
  qa_save_reader_destroy(&field);
  return has;
}

static void read_player(const qa_save_reader *reader, qa_q2_ctf_player_checkpoint *out) {
  static const double team_choices[3] = {0, 1, 2};
  qa_save_reader field;
  qa_q2_ctf_player_state_init(&out->state);
  if (!qa_save_reader_field(reader, "team", &field)) qa_q2f_error("Out of memory");
  out->state.team = qa_save_reader_choice_number(&field, team_choices, 3);
  qa_save_reader_destroy(&field);
  out->state.spawn_state = read_field_integer_min(reader, "spawnState", 0);
  out->state.has_last_hurt_carrier = read_nullable_finite(reader, "lastHurtCarrier", &out->state.last_hurt_carrier);
  out->state.has_last_returned_flag = read_nullable_finite(reader, "lastReturnedFlag", &out->state.last_returned_flag);
  out->state.has_last_fragged_carrier = read_nullable_finite(reader, "lastFraggedCarrier", &out->state.last_fragged_carrier);
  out->state.flag_since = read_field_finite(reader, "flagSince");
  out->state.voted = read_field_bool(reader, "voted");
  out->state.ready = read_field_bool(reader, "ready");
  out->state.admin = read_field_bool(reader, "admin");
  out->state.id_view = read_field_bool(reader, "idView");
  out->state.has_ghost_code = read_nullable_integer_min(reader, "ghostCode", 10000, &out->state.ghost_code);
  out->grapple.has_grapple = read_nullable_actor(reader, "grapple", &out->grapple.grapple);
  static const char *const grapple_choices[3] = {"fly", "pull", "hang"};
  out->grapple.grapple_state = read_field_choice(reader, "grappleState", grapple_choices, 3);
  out->grapple.grapple_release_time = read_field_finite(reader, "grappleReleaseTime");
  if (!qa_save_reader_field(reader, "grappleNoKnockback", &field)) qa_q2f_error("Out of memory");
  if (qa_save_reader_is_undefined(&field) || qa_save_reader_is_null(&field)) {
    out->grapple.has_no_knockback = false;
  } else {
    out->grapple.has_no_knockback = true;
    out->grapple.grapple_no_knockback = qa_save_reader_boolean(&field);
  }
  qa_save_reader_destroy(&field);
  out->state.regen_time = read_field_finite(reader, "regenTime");
  out->state.tech_sound_time = read_field_finite(reader, "techSoundTime");
  out->state.last_tech_message = read_field_finite(reader, "lastTechMessage");
  out->state.has_match_respawn_at = read_nullable_finite(reader, "matchRespawnAt", &out->state.match_respawn_at);
}

void qa_decode_q2_ctf_checkpoint(const uint8_t *bytes, size_t length, qa_q2_ctf_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_q2mp_xalloc(decoded);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q2-ctf")) qa_q2f_error("Out of memory");
  qa_save_reader field;
  if (!qa_save_reader_field(&reader, "version", &field)) qa_q2f_error("Out of memory");
  out->version = qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  qa_save_reader rules, match;
  if (!qa_save_reader_field(&reader, "rules", &rules)) qa_q2f_error("Out of memory");
  static const char *const force_choices[3] = {"", "red", "blue"};
  out->rules.force_join = read_field_choice(&rules, "forceJoin", force_choices, 3);
  out->rules.competition = read_field_integer_min(&rules, "competition", 0);
  out->rules.match_lock = read_field_bool(&rules, "matchLock");
  out->rules.election_percentage = read_field_finite(&rules, "electionPercentage");
  out->rules.match_minutes = read_field_finite(&rules, "matchMinutes");
  out->rules.setup_minutes = read_field_finite(&rules, "setupMinutes");
  out->rules.start_seconds = read_field_finite(&rules, "startSeconds");
  out->rules.capture_limit = read_field_integer_min(&rules, "captureLimit", 0);
  out->rules.instant_weapons = read_field_bool(&rules, "instantWeapons");
  qa_save_reader_destroy(&rules);
  if (!qa_save_reader_field(&reader, "match", &match)) qa_q2f_error("Out of memory");
  qa_q2_ctf_match_checkpoint *mc = &out->match;
  mc->team1 = read_field_integer_min(&match, "team1", 0);
  mc->team2 = read_field_integer_min(&match, "team2", 0);
  mc->total1 = read_field_integer(&match, "total1");
  mc->total2 = read_field_integer(&match, "total2");
  mc->has_last_flag_capture = read_nullable_finite(&match, "lastFlagCapture", &mc->last_flag_capture);
  {
    static const double team_choices[2] = {1, 2};
    if (!qa_save_reader_field(&match, "lastCaptureTeam", &field)) qa_q2f_error("Out of memory");
    mc->has_last_capture_team = !qa_save_reader_is_null(&field);
    if (mc->has_last_capture_team) mc->last_capture_team = qa_save_reader_choice_number(&field, team_choices, 2);
    qa_save_reader_destroy(&field);
  }
  static const char *const phase_choices[5] = {"none", "setup", "pregame", "game", "post"};
  mc->phase = read_field_choice(&match, "phase", phase_choices, 5);
  mc->match_time = read_field_finite(&match, "matchTime");
  mc->last_time = read_field_integer(&match, "lastTime");
  if (!qa_save_reader_field(&match, "election", &field)) qa_q2f_error("Out of memory");
  mc->has_election = !qa_save_reader_is_null(&field);
  if (mc->has_election) {
    static const char *const kind_choices[3] = {"match", "admin", "map"};
    mc->election.kind = read_field_choice(&field, "kind", kind_choices, 3);
    mc->election.target = read_field_actor(&field, "target");
    mc->election.map = qa_q2mp_dup(read_field_string(&field, "map"));
    mc->election.message = qa_q2mp_dup(read_field_string(&field, "message"));
    mc->election.votes = read_field_integer_min(&field, "votes", 0);
    mc->election.needed = read_field_integer_min(&field, "needed", 1);
    mc->election.expires = read_field_finite(&field, "expires");
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&match, "ghosts", &field)) qa_q2f_error("Out of memory");
  mc->ghost_count = qa_save_reader_list_count(&field);
  mc->ghosts = qa_q2mp_xalloc(malloc((mc->ghost_count == 0 ? 1 : mc->ghost_count) * sizeof *mc->ghosts));
  for (size_t i = 0; i < mc->ghost_count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&field, i, &item)) qa_q2f_error("Out of memory");
    qa_q2_ctf_ghost_checkpoint *gc = &mc->ghosts[i];
    memset(gc, 0, sizeof *gc);
    gc->code = read_field_integer_min(&item, "code", 10000);
    static const double team_choices[2] = {1, 2};
    qa_save_reader team_field;
    if (!qa_save_reader_field(&item, "team", &team_field)) qa_q2f_error("Out of memory");
    gc->team = qa_save_reader_choice_number(&team_field, team_choices, 2);
    qa_save_reader_destroy(&team_field);
    gc->name = qa_q2mp_dup(read_field_string(&item, "name"));
    gc->has_actor = read_nullable_actor(&item, "actor", &gc->actor);
    gc->score = read_field_integer(&item, "score");
    gc->deaths = read_field_integer_min(&item, "deaths", 0);
    gc->kills = read_field_integer_min(&item, "kills", 0);
    gc->captures = read_field_integer_min(&item, "captures", 0);
    gc->base_defense = read_field_integer_min(&item, "baseDefense", 0);
    gc->carrier_defense = read_field_integer_min(&item, "carrierDefense", 0);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&match);
  if (!qa_save_reader_field(&reader, "players", &field)) qa_q2f_error("Out of memory");
  out->player_count = qa_save_reader_list_count(&field);
  out->players = qa_q2mp_xalloc(malloc((out->player_count == 0 ? 1 : out->player_count) * sizeof *out->players));
  for (size_t i = 0; i < out->player_count; i++) {
    qa_save_reader item, state;
    if (!qa_save_reader_item(&field, i, &item)) qa_q2f_error("Out of memory");
    out->players[i].actor = read_field_actor(&item, "actor");
    if (!qa_save_reader_field(&item, "state", &state)) qa_q2f_error("Out of memory");
    read_player(&state, &out->players[i].state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  /* Reader results point at static choice strings; names were copied. */
  qa_save_value_free(decoded);
}
