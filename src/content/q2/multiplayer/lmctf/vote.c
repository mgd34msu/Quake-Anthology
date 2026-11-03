/* Port of src/content/q2/multiplayer/lmctf/vote.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/vote.h"
#include "../multiplayer_internal.h"
#include "qa/foundation/js.h"

struct qa_lmctf_vote {
  qa_lmctf_context *context;
  bool has_started;
  double started_at;
};

qa_lmctf_vote *qa_lmctf_vote_create(qa_lmctf_context *context) {
  qa_lmctf_vote *v = calloc(1, sizeof *v);
  if (v == NULL) return NULL;
  v->context = context;
  return v;
}
void qa_lmctf_vote_destroy(qa_lmctf_vote *v) { free(v); }
bool qa_lmctf_vote_started_at(qa_lmctf_vote *v, double *out) {
  if (v->has_started) *out = v->started_at;
  return v->has_started;
}

static bool vote_player(qa_lmctf_vote *v, qa_q2_game_services *g, size_t i) {
  qa_actor_id actor = v->context->states[i].actor;
  qa_q2_base_player_state *player = v->context->hooks.player(v->context->hooks.ctx, actor);
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
  return player != NULL && player->connected && entity != NULL && strcmp(entity->classname, "player") == 0;
}

void qa_lmctf_vote_menu(qa_lmctf_vote *v, qa_actor_id actor) {
  double ballot = qa_lmctf_player(v->context, actor)->extra_flags;
  qa_lmctf_event event = {0};
  event.kind = "menu";
  event.actor = actor;
  event.title = "LMCTF Vote Menu";
  static const qa_lmctf_menu_entry idle_entries[2] = {
      {"Skip to next map", "lmctf-vote skip"}, {"Vote:     Idle", NULL}};
  qa_lmctf_menu_entry active_entries[4] = {
      {"Vote YES", "voteyes"}, {"Vote NO", "voteno"}, {"Vote:     Started", NULL},
      {NULL, NULL}};
  if (!v->has_started) {
    event.entries = idle_entries;
    event.entry_count = 2;
  } else {
    active_entries[3].label = (qa_js_to_int32(ballot) & 64) != 0 ? "You have voted YES"
                              : (qa_js_to_int32(ballot) & 128) != 0 ? "You have voted NO" : "You have not voted";
    event.entries = active_entries;
    event.entry_count = 4;
  }
  v->context->hooks.emit(v->context->hooks.ctx, event);
}

void qa_lmctf_vote_start(qa_lmctf_vote *v, qa_actor_id actor, qa_q2_game_services *g) {
  size_t count = 0;
  for (size_t i = 0; i < v->context->state_count; i++)
    if (vote_player(v, g, i)) {
      count++;
      if (v->has_started) continue;
      qa_lmctf_player_state *state = &v->context->states[i].state;
      state->extra_flags = (double)(qa_js_to_int32(state->extra_flags) & ~192);
    }
  if (count < 4) {
    qa_lmctf_print(g, &actor, "You need at least four players on the server to initiate a vote\n");
  } else if (v->has_started) {
    qa_lmctf_print(g, &actor, "Vote has already been started\n");
  } else {
    v->has_started = true;
    v->started_at = qa_q2mp_now(g);
    qa_lmctf_player_state *starter = qa_lmctf_player(v->context, actor);
    starter->extra_flags = (double)(qa_js_to_int32(starter->extra_flags) | 64);
    qa_lmctf_print(g, NULL, "%s started vote to skip level.\n", qa_lmctf_name(v->context, actor));
    qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
    if (entity != NULL) g->sound(g->ctx, entity->actor, "misc/secret.wav", 3, 1, 0);
  }
  qa_lmctf_vote_menu(v, actor);
}

void qa_lmctf_vote_ballot(qa_lmctf_vote *v, qa_actor_id actor, qa_q2_game_services *g, bool yes) {
  if (!v->has_started) {
    qa_lmctf_print(g, &actor, "A vote has not been initiated.\n");
    return;
  }
  qa_lmctf_player_state *player = qa_lmctf_player(v->context, actor);
  player->extra_flags = (double)((qa_js_to_int32(player->extra_flags) & ~192) | (yes ? 64 : 128));
  qa_lmctf_print(g, &actor, "You have voted %s\n", yes ? "YES" : "NO");
  qa_lmctf_vote_menu(v, actor);
}

void qa_lmctf_vote_frame(qa_lmctf_vote *v, qa_q2_game_services *g) {
  if (!v->has_started || qa_q2mp_now(g) <= v->started_at + 30) return;
  v->has_started = false;
  qa_lmctf_print(g, NULL, "Vote session has ended\n");
  double yes = 0, no = 0, abstained = 0;
  for (size_t i = 0; i < v->context->state_count; i++) {
    if (!vote_player(v, g, i)) continue;
    int32_t flags = qa_js_to_int32(v->context->states[i].state.extra_flags); /* TS flags & x: ToInt32 */
    if ((flags & 64) != 0) yes++;
    else if ((flags & 128) != 0) no++;
    else abstained++;
  }
  char yes_text[32], no_text[32], abstained_text[32];
  qa_q2mp_format_number(yes_text, sizeof yes_text, yes);
  qa_q2mp_format_number(no_text, sizeof no_text, no);
  qa_q2mp_format_number(abstained_text, sizeof abstained_text, abstained);
  qa_lmctf_print(g, NULL, "VOTE RESULT: YES:%s  NO:%s  Abstained:%s\n", yes_text, no_text, abstained_text);
  double total = yes + no;
  if (total < 2) {
    qa_lmctf_print(g, NULL, "Vote Fails: you need at least 2 ballots cast!\n");
    return;
  }
  double percentage = trunc(yes * 100 / total);
  if (fmod(yes * 100, total) > qa_js_shr(total, 1)) percentage++;
  if (percentage < 75) {
    char pct[32];
    qa_q2mp_format_number(pct, sizeof pct, 100 - percentage);
    qa_lmctf_print(g, NULL, "Vote Fails with %s percent majority\n", pct);
    return;
  }
  char pct[32];
  qa_q2mp_format_number(pct, sizeof pct, percentage);
  qa_lmctf_print(g, NULL, "Vote to skip level Passes with %s percent majority\n", pct);
  v->context->hooks.end_level(v->context->hooks.ctx, g, NULL);
}

bool qa_lmctf_vote_capture(qa_lmctf_vote *v, double *out_started_at) {
  if (v->has_started) *out_started_at = v->started_at;
  return v->has_started;
}

void qa_lmctf_vote_restore(qa_lmctf_vote *v, const qa_save_reader *reader) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, "startedAt", &field)) qa_raise("Error", 0, 0, "Out of memory");
  v->has_started = !qa_save_reader_is_null(&field);
  if (v->has_started) v->started_at = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
}
