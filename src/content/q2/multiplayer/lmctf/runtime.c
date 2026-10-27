/* Port of src/content/q2/multiplayer/lmctf/runtime.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/runtime.h"
#include "../multiplayer_internal.h"
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/multiplayer/lmctf/admin.h"
#include "qa/content/q2/multiplayer/lmctf/presentation.h"
#include "qa/content/q2/multiplayer/lmctf/spawns.h"
#include "qa/persistence/save_image.h"
#include <ctype.h>

struct qa_q2_lmctf {
  qa_lmctf_hooks hooks;
  qa_lmctf_rules rules;
  bool has_travel;
  qa_lmctf_travel travel;
  qa_lmctf_context context;
  qa_lmctf_match *match;
  qa_lmctf_vote *vote;
  qa_lmctf_weapons *weapons;
  qa_lmctf_flags *flags;
  qa_lmctf_runes *runes;
  qa_lmctf_grapple *grapple;
  qa_q2_named_think *thinks;
  qa_q2_named_touch *touchs;
  qa_q2_callback_definitions callbacks;
};

static bool rule_can_score(void *ctx) { return qa_lmctf_match_can_score(ctx); }
static bool rule_flags_touchable(void *ctx) {
  return strcmp(qa_lmctf_match_phase(ctx), "countdown") != 0;
}
static qa_q2_entity *red_flag(void *ctx, qa_q2_game_services *g) {
  qa_q2_lmctf *lmctf = ctx;
  return qa_lmctf_flags_flag(lmctf->flags, 1, g);
}

struct frame_repeat { qa_lmctf_runes *runes; qa_q2mp_weapon_context *context; };
static void rules_post_native_think(void *ctx, qa_q2mp_weapon_context *context, void *repeat_ctx, void (*repeat)(void *repeat_ctx)) {
  qa_q2_lmctf *lmctf = ctx;
  qa_lmctf_runes_weapon_frame(lmctf->runes, context->actor, context->game, context->state->animation.source_firing, repeat, repeat_ctx);
}

#define MERGE_L(field, type)                                                                 \
  do {                                                                                       \
    size_t total = fc.field##_count + rc.field##_count + hc.field##_count + wc.field##_count; \
    if (total > 0) {                                                                         \
      lmctf->field##s = qa_q2mp_xalloc(malloc(total * sizeof *lmctf->field##s));              \
      size_t at = 0;                                                                         \
      for (size_t i = 0; i < fc.field##_count; i++) lmctf->field##s[at++] = fc.field[i];      \
      for (size_t i = 0; i < rc.field##_count; i++) lmctf->field##s[at++] = rc.field[i];      \
      for (size_t i = 0; i < hc.field##_count; i++) lmctf->field##s[at++] = hc.field[i];      \
      for (size_t i = 0; i < wc.field##_count; i++) lmctf->field##s[at++] = wc.field[i];      \
      lmctf->callbacks.field = lmctf->field##s;                                              \
      lmctf->callbacks.field##_count = at;                                                   \
    }                                                                                        \
  } while (0)

qa_q2_lmctf *qa_q2_lmctf_create(qa_lmctf_hooks hooks, const qa_lmctf_rules *rules,
                                const qa_lmctf_travel *travel, qa_shared_grapple_control *shared_grapple) {
  qa_q2_lmctf *lmctf = calloc(1, sizeof *lmctf);
  if (lmctf == NULL) return NULL;
  lmctf->hooks = hooks;
  if (rules != NULL) qa_lmctf_rules_copy(&lmctf->rules, rules);
  else qa_create_lmctf_rules(&lmctf->rules);
  if (travel != NULL) {
    lmctf->has_travel = true;
    qa_lmctf_rules_copy(&lmctf->travel.rules, &travel->rules);
    lmctf->travel.countdown = travel->countdown;
    lmctf->travel.paused = travel->paused;
    lmctf->travel.player_count = travel->player_count;
    lmctf->travel.players = qa_q2mp_xalloc(malloc((travel->player_count == 0 ? 1 : travel->player_count) * sizeof *lmctf->travel.players));
    for (size_t i = 0; i < travel->player_count; i++) lmctf->travel.players[i] = travel->players[i];
  }
  lmctf->context.hooks = hooks;
  lmctf->context.rules = &lmctf->rules;
  lmctf->context.plasma_quad = false;
  lmctf->match = qa_lmctf_match_create(&lmctf->context);
  if (lmctf->match == NULL) {
    qa_q2_lmctf_destroy(lmctf);
    return NULL;
  }
  lmctf->context.rule_ctx = lmctf->match;
  lmctf->context.can_score = rule_can_score;
  lmctf->context.flags_touchable = rule_flags_touchable;
  qa_lmctf_match_set_paused(lmctf->match, travel != NULL && travel->paused);
  lmctf->vote = qa_lmctf_vote_create(&lmctf->context);
  lmctf->weapons = qa_lmctf_weapons_create(&lmctf->context);
  lmctf->flags = qa_lmctf_flags_create(&lmctf->context);
  lmctf->runes = qa_lmctf_runes_create(&lmctf->context, red_flag, lmctf);
  lmctf->grapple = qa_lmctf_grapple_module_create(&lmctf->context, shared_grapple);
  if (lmctf->vote == NULL || lmctf->weapons == NULL || lmctf->flags == NULL || lmctf->runes == NULL || lmctf->grapple == NULL) {
    qa_q2_lmctf_destroy(lmctf);
    return NULL;
  }
  qa_q2_callback_definitions fc = qa_lmctf_flags_callbacks(lmctf->flags);
  qa_q2_callback_definitions rc = qa_lmctf_runes_callbacks(lmctf->runes);
  qa_q2_callback_definitions hc = qa_lmctf_grapple_module_callbacks(lmctf->grapple);
  qa_q2_callback_definitions wc = qa_lmctf_weapons_callbacks(lmctf->weapons);
  MERGE_L(think, qa_q2_named_think);
  MERGE_L(touch, qa_q2_named_touch);
  if (hc.die_count > 0) {
    lmctf->callbacks.die = hc.die;
    lmctf->callbacks.die_count = hc.die_count;
  }
  qa_q2mp_source_rules source_rules = {0};
  source_rules.kind = "lmctf";
  source_rules.ctx = lmctf;
  source_rules.post_native_think = rules_post_native_think;
  hooks.weapons->set_source_rules(hooks.weapons->ctx, source_rules);
  return lmctf;
}

void qa_q2_lmctf_destroy(qa_q2_lmctf *lmctf) {
  if (lmctf == NULL) return;
  qa_lmctf_match_destroy(lmctf->match);
  qa_lmctf_vote_destroy(lmctf->vote);
  qa_lmctf_weapons_destroy(lmctf->weapons);
  qa_lmctf_flags_destroy(lmctf->flags);
  qa_lmctf_runes_destroy(lmctf->runes);
  qa_lmctf_grapple_module_destroy(lmctf->grapple);
  free(lmctf->thinks);
  free(lmctf->touchs);
  qa_lmctf_context_clear(&lmctf->context);
  free(lmctf->context.states);
  if (lmctf->has_travel) qa_lmctf_travel_destroy(&lmctf->travel);
  qa_lmctf_rules_destroy(&lmctf->rules);
  free(lmctf);
}

static bool spawn_cb(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) { return qa_q2_lmctf_spawn(ctx, entity, g); }
qa_q2_spawn_module qa_q2_lmctf_as_spawn(qa_q2_lmctf *lmctf) {
  qa_q2_spawn_module m = {0};
  m.ctx = lmctf;
  m.callbacks = &lmctf->callbacks;
  m.spawn = spawn_cb;
  return m;
}
qa_lmctf_context *qa_q2_lmctf_get_context(qa_q2_lmctf *lmctf) { return &lmctf->context; }
qa_lmctf_rules *qa_q2_lmctf_get_rules(qa_q2_lmctf *lmctf) { return &lmctf->rules; }
qa_lmctf_match *qa_q2_lmctf_get_match(qa_q2_lmctf *lmctf) { return lmctf->match; }
qa_lmctf_vote *qa_q2_lmctf_get_vote(qa_q2_lmctf *lmctf) { return lmctf->vote; }
qa_lmctf_weapons *qa_q2_lmctf_get_weapons(qa_q2_lmctf *lmctf) { return lmctf->weapons; }
qa_lmctf_flags *qa_q2_lmctf_get_flags(qa_q2_lmctf *lmctf) { return lmctf->flags; }
qa_lmctf_runes *qa_q2_lmctf_get_runes(qa_q2_lmctf *lmctf) { return lmctf->runes; }
qa_lmctf_grapple *qa_q2_lmctf_get_grapple(qa_q2_lmctf *lmctf) { return lmctf->grapple; }
qa_q2_callback_definitions qa_q2_lmctf_callbacks(qa_q2_lmctf *lmctf) { return lmctf->callbacks; }

bool qa_q2_lmctf_spawn(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (strcmp(entity->classname, "item_flag_team1") == 0) entity->classname = "info_flag_red";
  else if (strcmp(entity->classname, "item_flag_team2") == 0) entity->classname = "info_flag_blue";
  else if (strcmp(entity->classname, "info_player_team1") == 0) entity->classname = "info_player_red";
  else if (strcmp(entity->classname, "info_player_team2") == 0) entity->classname = "info_player_blue";
  if (strcmp(entity->classname, "info_player_red") == 0 || strcmp(entity->classname, "info_player_blue") == 0) {
    g->solid(g->ctx, entity, "none");
    return true;
  }
  if (strcmp(entity->classname, "info_position") == 0) {
    g->solid(g->ctx, entity, "none");
    return true;
  }
  if (qa_lmctf_flags_spawn(lmctf->flags, entity, g) || qa_lmctf_runes_spawn(lmctf->runes, entity, g)) return true;
  if (strcmp(entity->classname, "item_invulnerability") == 0 && ((int64_t)lmctf->rules.ctf_flags & 2) == 0 &&
      g->options.mode == QA_Q2_MODE_DEATHMATCH) {
    g->remove(g->ctx, entity);
    return true;
  }
  return false;
}

void qa_q2_lmctf_post_spawn(qa_q2_lmctf *lmctf, qa_q2_game_services *g) {
  qa_q2_source_callbacks_register(g->source_callbacks, lmctf->callbacks);
  qa_lmctf_flags_post_spawn(lmctf->flags, g);
  qa_lmctf_runes_post_spawn(lmctf->runes, g);
  if (lmctf->has_travel && lmctf->travel.countdown) qa_lmctf_match_start(lmctf->match, g);
}

void qa_q2_lmctf_capture_travel(qa_q2_lmctf *lmctf, qa_lmctf_travel *out) {
  memset(out, 0, sizeof *out);
  qa_lmctf_rules_copy(&out->rules, &lmctf->rules);
  const qa_lmctf_map_change *pending = qa_lmctf_match_pending_map(lmctf->match);
  out->countdown = pending != NULL && pending->countdown;
  out->paused = qa_lmctf_match_paused(lmctf->match);
  out->players = qa_q2mp_xalloc(malloc((lmctf->context.state_count == 0 ? 1 : lmctf->context.state_count) * sizeof *out->players));
  for (size_t i = 0; i < lmctf->context.state_count; i++) {
    qa_actor_id actor = lmctf->context.states[i].actor;
    qa_lmctf_player_state *state = &lmctf->context.states[i].state;
    qa_q2mp_player_state *player = lmctf->hooks.player(lmctf->hooks.ctx, actor);
    if (player == NULL) continue;
    out->players[out->player_count++] = (qa_lmctf_travel_player){player->slot, state->team, state->observer_team, state->extra_flags};
  }
}

void qa_lmctf_capture_destroy(qa_lmctf_capture *capture) {
  free(capture->flags.flags);
  for (size_t i = 0; i < capture->player_count; i++) {
    for (size_t j = 0; j < capture->players[i].statistic_count; j++) free(capture->players[i].statistics[j].key);
    free(capture->players[i].statistics);
  }
  free(capture->players);
  free(capture->rules.map_list);
  free(capture->match.pending_map.map);
  memset(capture, 0, sizeof *capture);
}

void qa_q2_lmctf_capture(qa_q2_lmctf *lmctf, qa_lmctf_capture *out) {
  memset(out, 0, sizeof *out);
  const qa_lmctf_rules *rules = &lmctf->rules;
  out->rules.time_limit_minutes = rules->time_limit_minutes;
  out->rules.frag_limit = rules->frag_limit;
  out->rules.map_count = rules->map_count;
  out->rules.map_list = qa_q2mp_xalloc(malloc((rules->map_count == 0 ? 1 : rules->map_count) * sizeof *out->rules.map_list));
  for (size_t i = 0; i < rules->map_count; i++) out->rules.map_list[i] = rules->map_list[i];
  out->rules.ctf_flags = rules->ctf_flags;
  out->rules.ref_flags = rules->ref_flags;
  out->rules.runes = rules->runes;
  out->rules.skin_set = rules->skin_set;
  out->rules.flag_init = rules->flag_init;
  out->rules.disabled_weapons = rules->disabled_weapons;
  out->rules.fast_switch = rules->fast_switch;
  out->rules.auto_lock = rules->auto_lock;
  out->rules.countdown_seconds = rules->countdown_seconds;
  out->rules.quad_seconds = rules->quad_seconds;
  out->match = qa_lmctf_match_capture(lmctf->match);
  if (out->match.has_pending_map) out->match.pending_map.map = qa_q2mp_dup(out->match.pending_map.map);
  out->has_vote_started = qa_lmctf_vote_capture(lmctf->vote, &out->vote_started_at);
  out->plasma_quad = lmctf->context.plasma_quad;
  qa_lmctf_flags_capture(lmctf->flags, &out->flags);
  out->runes_forward = qa_lmctf_runes_capture(lmctf->runes);
  out->player_count = lmctf->context.state_count;
  out->players = qa_q2mp_xalloc(malloc((out->player_count == 0 ? 1 : out->player_count) * sizeof *out->players));
  for (size_t i = 0; i < out->player_count; i++) {
    qa_actor_id actor = lmctf->context.states[i].actor;
    qa_lmctf_player_state *state = &lmctf->context.states[i].state;
    qa_lmctf_player_checkpoint_entry *entry = &out->players[i];
    memset(entry, 0, sizeof *entry);
    entry->actor = qa_save_ctf_actor(actor);
    entry->plasma_mode = state->plasma_mode;
    entry->team = state->team;
    entry->observer_team = state->observer_team;
    entry->has_rune = state->has_rune;
    if (state->has_rune) entry->rune = qa_save_ctf_actor(state->rune);
    entry->grapple = qa_capture_lmctf_grapple(qa_lmctf_grapple_module_state(lmctf->grapple, actor));
    entry->regen_frame = state->regen_frame;
    entry->kill_carrier_time = state->kill_carrier_time;
    entry->hit_carrier_time = state->hit_carrier_time;
    entry->return_flag_time = state->return_flag_time;
    entry->defend_flag_time = state->defend_flag_time;
    entry->extra_flags = state->extra_flags;
    entry->spawn_state = state->spawn_state;
    entry->statistic_count = state->statistic_count;
    entry->statistics = qa_q2mp_xalloc(malloc((state->statistic_count == 0 ? 1 : state->statistic_count) * sizeof *entry->statistics));
    for (size_t j = 0; j < state->statistic_count; j++) {
      entry->statistics[j].key = qa_q2mp_dup(state->statistics[j].key);
      entry->statistics[j].count = state->statistics[j].count;
    }
  }
}

static double rd_finite(const qa_save_reader *r, const char *name) {
  qa_save_reader f;
  if (!qa_save_reader_field(r, name, &f)) qa_q2f_error("Out of memory");
  double v = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static double rd_integer(const qa_save_reader *r, const char *name) {
  qa_save_reader f;
  if (!qa_save_reader_field(r, name, &f)) qa_q2f_error("Out of memory");
  double v = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static double rd_integer_min(const qa_save_reader *r, const char *name, double minimum) {
  qa_save_reader f;
  if (!qa_save_reader_field(r, name, &f)) qa_q2f_error("Out of memory");
  double v = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return v;
}
static bool rd_bool(const qa_save_reader *r, const char *name) {
  qa_save_reader f;
  if (!qa_save_reader_field(r, name, &f)) qa_q2f_error("Out of memory");
  bool v = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static void rd_sub(const qa_save_reader *r, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(r, name, out)) qa_q2f_error("Out of memory");
}

void qa_q2_lmctf_restore(qa_q2_lmctf *lmctf, const qa_save_reader *reader, qa_q2_game_services *g) {
  qa_lmctf_grapple_equipment *equipment = qa_lmctf_grapple_module_equipment(lmctf->grapple);
  if (equipment != NULL) qa_lmctf_grapple_bind(equipment, g);
  qa_save_reader rules;
  rd_sub(reader, "rules", &rules);
  lmctf->rules.time_limit_minutes = rd_finite(&rules, "timeLimitMinutes");
  lmctf->rules.frag_limit = rd_integer(&rules, "fragLimit");
  {
    qa_save_reader list;
    rd_sub(&rules, "mapList", &list);
    size_t count = qa_save_reader_list_count(&list);
    const char **maps = qa_q2mp_xalloc(malloc((count == 0 ? 1 : count) * sizeof *maps));
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&list, i, &item)) qa_q2f_error("Out of memory");
      /* Restored strings are owned copies (TS GC strings); the rules
       * array is freed by destroy, the strings intentionally leak with
       * the module lifetime. */
      maps[i] = qa_q2mp_dup(qa_save_reader_string(&item));
      qa_save_reader_destroy(&item);
    }
    free(lmctf->rules.map_list);
    lmctf->rules.map_list = maps;
    lmctf->rules.map_count = count;
    qa_save_reader_destroy(&list);
  }
  lmctf->rules.ctf_flags = rd_integer_min(&rules, "ctfFlags", 0);
  lmctf->rules.ref_flags = rd_integer_min(&rules, "refFlags", 0);
  lmctf->rules.runes = rd_integer_min(&rules, "runes", 0);
  lmctf->rules.skin_set = rd_integer(&rules, "skinSet");
  lmctf->rules.flag_init = rd_bool(&rules, "flagInit");
  lmctf->rules.disabled_weapons = rd_integer_min(&rules, "disabledWeapons", 0);
  lmctf->rules.fast_switch = rd_bool(&rules, "fastSwitch");
  lmctf->rules.auto_lock = rd_bool(&rules, "autoLock");
  lmctf->rules.countdown_seconds = rd_finite(&rules, "countdownSeconds");
  lmctf->rules.quad_seconds = rd_finite(&rules, "quadSeconds");
  qa_save_reader_destroy(&rules);
  qa_lmctf_grapple_module_states_clear(lmctf->grapple);
  qa_save_reader players_reader;
  rd_sub(reader, "players", &players_reader);
  size_t player_count = qa_save_reader_list_count(&players_reader);
  struct restored { qa_actor_id actor; qa_lmctf_player_state state; };
  struct restored *restored = qa_q2mp_xalloc(malloc((player_count == 0 ? 1 : player_count) * sizeof *restored));
  static const double team_choices[3] = {0, 1, 2};
  static const double hook_choices[3] = {0, 1, 2};
  for (size_t i = 0; i < player_count; i++) {
    qa_save_reader entry, saved, field;
    if (!qa_save_reader_item(&players_reader, i, &entry)) qa_q2f_error("Out of memory");
    rd_sub(&entry, "actor", &field);
    qa_saved_actor_id saved_actor = qa_read_saved_actor(&field);
    qa_save_reader_destroy(&field);
    qa_owned_actor owned;
    if (!qa_session_actor_registry_resolve_saved(g->host->actors, saved_actor, &owned) ||
        lmctf->hooks.player(lmctf->hooks.ctx, owned.id) == NULL) {
      qa_save_reader_destroy(&entry);
      qa_save_reader_destroy(&players_reader);
      free(restored);
      qa_q2f_error("LMCTF restore requires an admitted source player");
    }
    restored[i].actor = owned.id;
    rd_sub(&entry, "state", &saved);
    qa_lmctf_player_state *state = &restored[i].state;
    qa_lmctf_player_state_init(state);
    state->plasma_mode = rd_bool(&saved, "plasmaMode");
    rd_sub(&saved, "team", &field);
    state->team = qa_save_reader_choice_number(&field, team_choices, 3);
    qa_save_reader_destroy(&field);
    rd_sub(&saved, "observerTeam", &field);
    state->observer_team = qa_save_reader_choice_number(&field, team_choices, 3);
    qa_save_reader_destroy(&field);
    rd_sub(&saved, "rune", &field);
    state->has_rune = !qa_save_reader_is_null(&field);
    if (state->has_rune)
      state->rune = qa_session_actor_registry_reference_saved(g->host->actors, qa_read_saved_actor(&field), "checkpoint");
    qa_save_reader_destroy(&field);
    if (equipment != NULL) {
      qa_lmctf_grapple_checkpoint grapple = {0};
      rd_sub(&saved, "hook", &field);
      grapple.has_hook = !qa_save_reader_is_null(&field);
      if (grapple.has_hook) grapple.hook = qa_read_saved_actor(&field);
      qa_save_reader_destroy(&field);
      rd_sub(&saved, "hookState", &field);
      grapple.hook_state = qa_save_reader_choice_number(&field, hook_choices, 3);
      qa_save_reader_destroy(&field);
      grapple.hook_length = rd_finite(&saved, "hookLength");
      grapple.hook_held = rd_bool(&saved, "hookHeld");
      *qa_lmctf_grapple_state_of(equipment, owned.id) = qa_restore_lmctf_grapple(grapple, g);
    }
    state->regen_frame = rd_integer(&saved, "regenFrame");
    state->kill_carrier_time = rd_finite(&saved, "killCarrierTime");
    state->hit_carrier_time = rd_finite(&saved, "hitCarrierTime");
    state->return_flag_time = rd_finite(&saved, "returnFlagTime");
    state->defend_flag_time = rd_finite(&saved, "defendFlagTime");
    state->extra_flags = rd_integer(&saved, "extraFlags");
    state->spawn_state = rd_integer_min(&saved, "spawnState", 0);
    qa_save_reader stats;
    rd_sub(&saved, "statistics", &stats);
    size_t stat_count = qa_save_reader_list_count(&stats);
    for (size_t j = 0; j < stat_count; j++) {
      qa_save_reader item, key_field;
      if (!qa_save_reader_item(&stats, j, &item)) qa_q2f_error("Out of memory");
      rd_sub(&item, "key", &key_field);
      const char *key = qa_save_reader_string(&key_field);
      double count = rd_finite(&item, "count");
      qa_lmctf_player_state_set_stat(state, key, count);
      qa_save_reader_destroy(&key_field);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&stats);
    qa_save_reader_destroy(&saved);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&players_reader);
  qa_save_reader sub;
  rd_sub(reader, "match", &sub);
  qa_lmctf_match_restore(lmctf->match, &sub);
  qa_save_reader_destroy(&sub);
  rd_sub(reader, "vote", &sub);
  qa_lmctf_vote_restore(lmctf->vote, &sub);
  qa_save_reader_destroy(&sub);
  lmctf->context.plasma_quad = rd_bool(reader, "plasmaQuad");
  rd_sub(reader, "flags", &sub);
  {
    qa_save_reader flags_list;
    rd_sub(&sub, "flags", &flags_list);
    size_t flag_count = qa_save_reader_list_count(&flags_list);
    qa_lmctf_flags_checkpoint checkpoint = {0};
    checkpoint.flags = qa_q2mp_xalloc(malloc((flag_count == 0 ? 1 : flag_count) * sizeof *checkpoint.flags));
    checkpoint.flag_count = flag_count;
    checkpoint.last_taken_sound = rd_finite(&sub, "lastTakenSound");
    static const double flag_teams[2] = {1, 2};
    for (size_t i = 0; i < flag_count; i++) {
      qa_save_reader item, field;
      if (!qa_save_reader_item(&flags_list, i, &item)) qa_q2f_error("Out of memory");
      rd_sub(&item, "team", &field);
      checkpoint.flags[i].team = qa_save_reader_choice_number(&field, flag_teams, 2);
      qa_save_reader_destroy(&field);
      rd_sub(&item, "actor", &field);
      checkpoint.flags[i].actor = qa_read_saved_actor(&field);
      qa_save_reader_destroy(&field);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&flags_list);
    qa_lmctf_flags_restore(lmctf->flags, g, checkpoint);
    free(checkpoint.flags);
  }
  qa_save_reader_destroy(&sub);
  rd_sub(reader, "runes", &sub);
  qa_lmctf_runes_restore(lmctf->runes, rd_bool(&sub, "forward"));
  qa_save_reader_destroy(&sub);
  qa_lmctf_context_clear(&lmctf->context);
  for (size_t i = 0; i < player_count; i++) qa_lmctf_context_set(&lmctf->context, restored[i].actor, restored[i].state);
  free(restored);
}

void qa_q2_lmctf_team_totals(qa_q2_lmctf *lmctf, double out[2]) {
  double red = 0, blue = 0;
  for (size_t i = 0; i < lmctf->context.state_count; i++) {
    qa_actor_id actor = lmctf->context.states[i].actor;
    qa_lmctf_player_state *state = &lmctf->context.states[i].state;
    qa_q2mp_player_state *player = lmctf->hooks.player(lmctf->hooks.ctx, actor);
    double score = player == NULL ? 0 : player->score;
    if (state->team == 1) red += score;
    else if (state->team == 2) blue += score;
  }
  out[0] = red;
  out[1] = blue;
}

static void set_team(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_lmctf_playing_team team) {
  qa_lmctf_player_state *state = qa_lmctf_player(&lmctf->context, entity->actor.id);
  qa_q2mp_player_state *player = lmctf->hooks.player(lmctf->hooks.ctx, entity->actor.id);
  state->team = team;
  state->observer_team = 0;
  if (player != NULL) {
    player->spectator = false;
    player->requested_spectator = false;
    player->noclip = false;
  }
  qa_combat_trait_changes traits = {0};
  traits.has_team = true;
  traits.team = ((int64_t)lmctf->rules.ctf_flags & 128) != 0 ? NULL : team == 1 ? "RED" : "BLUE";
  qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
  qa_lmctf_print(g, NULL, "%s is now on the %s team.\n", qa_lmctf_name(&lmctf->context, entity->actor.id), team == 1 ? "red" : "blue");
}

void qa_q2_lmctf_admitted(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_grapple_equipment *equipment = qa_lmctf_grapple_module_equipment(lmctf->grapple);
  if (equipment != NULL) qa_lmctf_grapple_bind(equipment, g);
  if (qa_lmctf_context_get(&lmctf->context, entity->actor.id) != NULL) return;
  qa_q2mp_player_state *common = lmctf->hooks.player(lmctf->hooks.ctx, entity->actor.id);
  if (common == NULL) qa_q2f_error("LMCTF admission requires shared source player state");
  qa_lmctf_player_state fresh;
  qa_lmctf_player_state_init(&fresh);
  qa_lmctf_player_state *state = qa_lmctf_context_set(&lmctf->context, entity->actor.id, fresh);
  if (lmctf->has_travel) {
    for (size_t i = 0; i < lmctf->travel.player_count; i++) {
      const qa_lmctf_travel_player *carried = &lmctf->travel.players[i];
      if (carried->slot != common->slot) continue;
      state->extra_flags = carried->extra_flags;
      if (carried->team == 0) {
        qa_q2_lmctf_observer(lmctf, entity, g, carried->observer_team);
        return;
      }
      set_team(lmctf, entity, g, (qa_lmctf_playing_team)carried->team);
      return;
    }
  }
  if (common->spectator || common->requested_spectator) {
    qa_q2_lmctf_observer(lmctf, entity, g, 0);
    return;
  }
  double red = 0, blue = 0;
  for (size_t i = 0; i < lmctf->context.state_count; i++) {
    if (qa_same_actor(lmctf->context.states[i].actor, entity->actor.id)) continue;
    if (lmctf->context.states[i].state.team == 1) red++;
    else if (lmctf->context.states[i].state.team == 2) blue++;
  }
  double totals[2];
  qa_q2_lmctf_team_totals(lmctf, totals);
  state->team = red < blue ? 1 : blue < red ? 2 : totals[0] > totals[1] ? 2 : 1;
  set_team(lmctf, entity, g, (qa_lmctf_playing_team)state->team);
}

void qa_q2_lmctf_player_spawned(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_player_state *state = qa_lmctf_player(&lmctf->context, entity->actor.id);
  qa_lmctf_grapple_module_abort(lmctf->grapple, entity->actor, g);
  qa_lmctf_grapple_module_state_ref(lmctf->grapple, entity->actor.id)->hook_held = false;
  size_t en = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, entity->actor.id, &en);
  bool has_hook = false;
  for (size_t i = 0; i < en; i++)
    if (entries[i].item != NULL && strcmp(entries[i].item, "q2:weapon_hook") == 0) has_hook = true;
  if (!has_hook) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:weapon_hook"; entry.count = 0; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, entity->actor, entry, NULL, NULL);
  }
  if (qa_lmctf_grapple_module_native_enabled(lmctf->grapple) && state->team != 0)
    qa_shared_inventory_table_give(g->host->inventory, entity->actor, "q2:weapon_hook", 1);
  qa_shared_grapple_control *shared = qa_lmctf_grapple_module_shared(lmctf->grapple);
  if (shared != NULL && !shared->selection.is_enabled) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:weapon_hook"; entry.count = 0; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, entity->actor, entry, NULL, NULL);
  }
  qa_combat_trait_changes traits = {0};
  traits.has_team = true;
  traits.team = state->team == 0 || ((int64_t)lmctf->rules.ctf_flags & 128) != 0 ? NULL : state->team == 1 ? "RED" : "BLUE";
  qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
}

void qa_q2_lmctf_join(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_lmctf_playing_team team) {
  qa_lmctf_player_state *state = qa_lmctf_player(&lmctf->context, entity->actor.id);
  qa_q2mp_player_state *player = lmctf->hooks.player(lmctf->hooks.ctx, entity->actor.id);
  if (state->team == team || player == NULL) return;
  if (qa_lmctf_match_teams_locked(lmctf->match)) {
    qa_lmctf_print(g, &entity->actor.id, "Teams are locked.\n");
    return;
  }
  if (((int64_t)lmctf->rules.ctf_flags & 8) != 0) {
    qa_q2_presentation_event event = {0};
    event.kind = "centerprint";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.text = "Sorry.  Team switching has been turned\n off on this server.\n";
    g->host->emit(g->host->ctx, event);
    return;
  }
  if (!player->spectator) {
    g->damage(g->ctx, entity->actor.id, entity, entity->actor.id, &entity->actor.id, 100000, 0, QA_Q2_ZERO,
              qa_q2_game_body(g, entity).origin, QA_Q2_ZERO, 23, 32, NULL);
    qa_lmctf_score(&lmctf->context, g, entity->actor.id, 1, "Team Change", NULL);
    qa_lmctf_stat(&lmctf->context, entity->actor.id, "deaths", -1);
  }
  qa_q2_lmctf_drop_inventory(lmctf, entity, g);
  set_team(lmctf, entity, g, team);
  state->spawn_state = 0;
  lmctf->hooks.spawn_player(lmctf->hooks.ctx, entity, g);
  qa_q2_lmctf_player_spawned(lmctf, entity, g);
}

void qa_q2_lmctf_observer(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_lmctf_team team) {
  qa_lmctf_player_state *state = qa_lmctf_player(&lmctf->context, entity->actor.id);
  qa_q2mp_player_state *player = lmctf->hooks.player(lmctf->hooks.ctx, entity->actor.id);
  if (player == NULL) return;
  qa_q2_lmctf_drop_inventory(lmctf, entity, g);
  state->team = 0;
  state->observer_team = team;
  player->spectator = true;
  player->requested_spectator = true;
  player->noclip = true;
  qa_combat_trait_changes traits = {0};
  traits.has_team = true;
  traits.team = NULL;
  traits.has_can_take_damage = true;
  traits.can_take_damage = false;
  qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
  lmctf->hooks.observer(lmctf->hooks.ctx, entity, g);
}

void qa_q2_lmctf_select_spawn(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_select_lmctf_spawn(&lmctf->context, entity, g, out_origin, out_angles);
}

void qa_q2_lmctf_score_event(qa_q2_lmctf *lmctf, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g, double change, double means, qa_q2_entity *recipient) {
  (void)means;
  qa_lmctf_score(&lmctf->context, g, recipient->actor.id, change, change > 0 ? "Kill" : "Suicide", &victim->actor.id);
  if (attacker != NULL) qa_lmctf_flags_frag(lmctf->flags, victim, attacker, g);
}

void qa_q2_lmctf_drop_inventory(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_flags_drop(lmctf->flags, entity, g);
  qa_lmctf_runes_drop(lmctf->runes, entity->actor.id, g);
  qa_lmctf_grapple_state *hook = qa_lmctf_grapple_module_state_has(lmctf->grapple, entity->actor.id)
      ? qa_lmctf_grapple_module_state_ref(lmctf->grapple, entity->actor.id) : NULL;
  if (hook != NULL && hook->has_hook) qa_lmctf_grapple_module_abort(lmctf->grapple, entity->actor, g);
}

void qa_q2_lmctf_player_death(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_stat(&lmctf->context, entity->actor.id, "deaths", 1);
  qa_q2_lmctf_drop_inventory(lmctf, entity, g);
}

void qa_q2_lmctf_disconnect(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_lmctf_drop_inventory(lmctf, entity, g);
  qa_lmctf_context_delete(&lmctf->context, entity->actor.id);
  qa_lmctf_grapple_module_state_delete(lmctf->grapple, entity->actor.id);
}

void qa_q2_lmctf_player_frame(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_match_frame(lmctf->match, g);
  qa_lmctf_vote_frame(lmctf->vote, g);
  qa_lmctf_runes_player_frame(lmctf->runes, entity, g);
  qa_lmctf_grapple_state *hook = qa_lmctf_grapple_module_state_has(lmctf->grapple, entity->actor.id)
      ? qa_lmctf_grapple_module_state_ref(lmctf->grapple, entity->actor.id) : NULL;
  if (hook != NULL && hook->hook_state != 0) qa_lmctf_grapple_module_fire(lmctf->grapple, entity->actor, g);
}

bool qa_q2_lmctf_can_move(qa_q2_lmctf *lmctf, qa_actor_id actor) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(&lmctf->context, actor);
  double flags = state == NULL ? 0 : state->extra_flags;
  return !qa_lmctf_match_paused(lmctf->match) || ((int64_t)flags & 2) != 0;
}

double qa_q2_lmctf_gravity_scale(qa_q2_lmctf *lmctf, qa_actor_id actor) {
  return qa_lmctf_grapple_module_gravity_scale(lmctf->grapple, actor);
}

static void lower_into(char *out, size_t size, const char *text) {
  size_t i = 0;
  for (; text[i] && i + 1 < size; i++) out[i] = (char)tolower((unsigned char)text[i]);
  out[i] = '\0';
}
static char *join_args(const char *const *args, size_t count) {
  size_t len = 1;
  for (size_t i = 0; i < count; i++) len += strlen(args[i]) + 1;
  char *out = qa_q2mp_xalloc(malloc(len));
  out[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i > 0) strcat(out, " ");
    strcat(out, args[i]);
  }
  return out;
}

bool qa_q2_lmctf_command(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *g, const char *name_in, const char *const *args, size_t arg_count) {
  char name[256];
  lower_into(name, sizeof name, name_in);
  if (qa_lmctf_admin_command(&lmctf->context, lmctf->match, entity, g, name, args, arg_count)) return true;
  static const char *const pause_allowed[8] = {"ctfmenu", "voteyes", "voteno", "lmctf-vote", "score", "say", "say_team", "players"};
  bool allowed = strcmp(name, "playerlist") == 0;
  for (int i = 0; i < 8; i++)
    if (strcmp(name, pause_allowed[i]) == 0) allowed = true;
  if (!qa_q2_lmctf_can_move(lmctf, entity->actor.id) && !allowed) return true;
  if (strcmp(name, "ctfmenu") == 0) {
    qa_lmctf_menu(&lmctf->context, entity->actor.id);
    return true;
  }
  if (strcmp(name, "voteyes") == 0 || strcmp(name, "voteno") == 0) {
    qa_lmctf_vote_ballot(lmctf->vote, entity->actor.id, g, strcmp(name, "voteyes") == 0);
    return true;
  }
  if (strcmp(name, "lmctf-vote") == 0) {
    if (arg_count > 0 && strcmp(args[0], "skip") == 0) qa_lmctf_vote_start(lmctf->vote, entity->actor.id, g);
    else qa_lmctf_vote_menu(lmctf->vote, entity->actor.id);
    return true;
  }
  if (strcmp(name, "score") == 0) {
    qa_lmctf_scoreboard(&lmctf->context, entity->actor.id);
    return true;
  }
  if (strcmp(name, "hook") == 0 || strcmp(name, "+hook") == 0) {
    qa_lmctf_grapple_module_command(lmctf->grapple, entity, g, true);
    return true;
  }
  if (strcmp(name, "unhook") == 0 || strcmp(name, "-hook") == 0) {
    qa_lmctf_grapple_module_command(lmctf->grapple, entity, g, false);
    return true;
  }
  if (strcmp(name, "team") == 0) {
    char choice[64] = "";
    if (arg_count > 0) lower_into(choice, sizeof choice, args[0]);
    if (strcmp(choice, "red") == 0 || strcmp(choice, "blue") == 0) {
      qa_q2_lmctf_join(lmctf, entity, g, strcmp(choice, "red") == 0 ? 1 : 2);
    } else {
      qa_lmctf_print(g, &entity->actor.id, "You are currently on the %s team.\nUse 'team red' or 'team blue' to change teams.\n",
                     qa_lmctf_player(&lmctf->context, entity->actor.id)->team == 1 ? "red" : "blue");
    }
    return true;
  }
  if (strcmp(name, "observe") == 0 || strcmp(name, "observe_red") == 0 || strcmp(name, "observe_blue") == 0) {
    qa_q2_lmctf_observer(lmctf, entity, g, strcmp(name, "observe_red") == 0 ? 1 : strcmp(name, "observe_blue") == 0 ? 2 : 0);
    return true;
  }
  if (strcmp(name, "drop") == 0 || strcmp(name, "use") == 0) {
    char *joined = join_args(args, arg_count);
    char requested[512];
    lower_into(requested, sizeof requested, joined);
    free(joined);
    if (strcmp(requested, "flag") == 0 || strcmp(requested, "enemy flag") == 0) {
      qa_lmctf_flags_drop(lmctf->flags, entity, g);
      return true;
    }
    size_t len = strlen(requested);
    if (strcmp(requested, "rune") == 0 || (len >= 9 && strcmp(requested + len - 9, " artifact") == 0))
      return qa_lmctf_runes_drop(lmctf->runes, entity->actor.id, g);
    return false;
  }
  return false;
}
