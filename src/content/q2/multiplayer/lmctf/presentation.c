/* Port of src/content/q2/multiplayer/lmctf/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/presentation.h"
#include "../multiplayer_internal.h"
#include "qa/foundation/js.h"

static int row_cmp(const void *a, const void *b) {
  const qa_lmctf_score_row *ra = a, *rb = b;
  if (ra->score != rb->score) return rb->score > ra->score ? 1 : -1;
  return 0;
}

void qa_lmctf_scoreboard(qa_lmctf_context *context, qa_actor_id actor) {
  qa_lmctf_score_row *rows = qa_q2mp_xalloc(malloc((context->state_count == 0 ? 1 : context->state_count) * sizeof *rows));
  size_t count = 0;
  for (size_t i = 0; i < context->state_count; i++) {
    qa_actor_id id = context->states[i].actor;
    qa_lmctf_player_state *state = &context->states[i].state;
    qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, id);
    if (player != NULL && player->connected) {
      rows[count++] = (qa_lmctf_score_row){id, player->slot, player->name, state->team,
          qa_lmctf_player_state_stat(state, "score", 0), player->ping < 999 ? player->ping : 999};
    }
  }
  qa_js_sort(rows, count, sizeof *rows, row_cmp);
  char layout[2048];
  snprintf(layout, sizeof layout, "xv 0 yv 32 string2 \"Scr Png Name        \" xv 0 yv 40 string2 \"------------------- \" xv 160 yv 32 string2 \"Scr Png Name        \" xv 160 yv 40 string2 \"------------------- \" ");
  for (int team = 1; team <= 2; team++) {
    size_t shown = 0;
    for (size_t i = 0; i < count && shown < 21; i++) {
      if (rows[i].team != team) continue;
      char slot[32], score[32], ping[32], entry[160];
      qa_q2mp_format_number(slot, sizeof slot, rows[i].slot);
      qa_q2mp_format_number(score, sizeof score, rows[i].score);
      qa_q2mp_format_number(ping, sizeof ping, rows[i].ping);
      snprintf(entry, sizeof entry, "ctf %d %d %s %s %s ", team == 1 ? 0 : 160, 48 + (int)shown * 8, slot, score, ping);
      if (strlen(layout) + strlen(entry) <= 1024) strcat(layout, entry);
      shown++;
    }
  }
  qa_lmctf_event event = {0};
  event.kind = "scoreboard";
  event.actor = actor;
  event.rows = rows;
  event.row_count = count;
  event.layout = layout;
  context->hooks.emit(context->hooks.ctx, event);
  free(rows);
}

void qa_lmctf_menu(qa_lmctf_context *context, qa_actor_id actor) {
  qa_lmctf_menu_entry entries[4] = {
      {"Join Red Team", "team red"}, {"Join Blue Team", "team blue"}, {"Become Observer", "observe"},
      {"Voting Menu", "lmctf-vote"}};
  size_t count = 3;
  if ((qa_js_to_int32(context->rules->ctf_flags) & 32768) == 0) count = 4;
  qa_lmctf_event event = {0};
  event.kind = "menu";
  event.actor = actor;
  event.title = "LMCTF Menu";
  event.entries = entries;
  event.entry_count = count;
  context->hooks.emit(context->hooks.ctx, event);
}
