/* Port of src/content/q3/base/game/save-level.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_level.h"
#include <stdlib.h>
#include <string.h>
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
qa_q3_level_capture qa_q3_capture_level(qa_q3_game_level *level) {
  qa_q3_level_capture c; c.values = qa_q3_capture_level_values(level);
  c.team_score_count = level->team_scores.length; c.team_scores = malloc((c.team_score_count ? c.team_score_count : 1) * sizeof *c.team_scores);
  for (size_t i = 0; i < c.team_score_count; i++) c.team_scores[i] = level->team_scores.values[i];
  c.num_team_voting_clients[0] = level->base.num_team_voting_clients[0]; c.num_team_voting_clients[1] = level->base.num_team_voting_clients[1];
  c.sorted_count = QA_Q3_MAX_CLIENTS; c.sorted_clients = malloc(c.sorted_count * sizeof *c.sorted_clients);
  memcpy(c.sorted_clients, level->base.sorted_clients, c.sorted_count * sizeof *c.sorted_clients);
  c.vote = level->base.vote; c.vote.string = dupstr(level->base.vote.string);
  c.team_votes[0] = level->base.team_votes[0]; c.team_votes[0].string = dupstr(level->base.team_votes[0].string);
  c.team_votes[1] = level->base.team_votes[1]; c.team_votes[1].string = dupstr(level->base.team_votes[1].string); return c;
}
void qa_q3_level_capture_destroy(qa_q3_level_capture *c) {
  qa_q3_level_values_destroy(&c->values); free(c->team_scores); free(c->sorted_clients);
  free(c->vote.string); free(c->team_votes[0].string); free(c->team_votes[1].string);
}
static double *read_numbers(const qa_save_reader *r, const char *name, size_t *out_n) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); size_t n = qa_save_reader_list_count(&f);
  double *d = malloc((n ? n : 1) * sizeof *d);
  for (size_t i = 0; i < n; i++) { qa_save_reader item; qa_save_reader_item(&f, i, &item); d[i] = qa_save_reader_number(&item); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&f); *out_n = n; return d;
}
void qa_q3_restore_level(qa_q3_game_level *level, const qa_save_value *value) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.level"); qa_save_reader f;
  qa_save_reader_field(&reader, "values", &f); qa_q3_level_values vals = qa_q3_read_level_values(&f); qa_save_reader_destroy(&f);
  qa_q3_restore_level_values(level, &vals); qa_q3_level_values_destroy(&vals);
  size_t ns, nv, nsort; double *scores = read_numbers(&reader, "teamScores", &ns);
  double *voting = read_numbers(&reader, "numTeamVotingClients", &nv);
  double *sorted = read_numbers(&reader, "sortedClients", &nsort);
  qa_save_reader_field(&reader, "teamVotes", &f); size_t nt = qa_save_reader_list_count(&f); qa_save_reader_destroy(&f);
  if (ns != level->team_scores.length || nv != 2 || nsort != QA_Q3_MAX_CLIENTS || nt != 2) qa_save_reader_fail(&reader, "Q3 level table length mismatch");
  for (size_t i = 0; i < ns; i++) qa_q3_game_slots_set_mirror(&level->team_scores, i, scores[i]);
  level->base.num_team_voting_clients[0] = voting[0]; level->base.num_team_voting_clients[1] = voting[1];
  memcpy(level->base.sorted_clients, sorted, nsort * sizeof *sorted);
  free(scores); free(voting); free(sorted);
  qa_save_reader_field(&reader, "vote", &f); qa_save_reader vf;
  qa_save_reader_field(&f, "time", &vf); level->base.vote.time = qa_save_reader_number(&vf); qa_save_reader_destroy(&vf);
  qa_save_reader_field(&f, "yes", &vf); level->base.vote.yes = qa_save_reader_number(&vf); qa_save_reader_destroy(&vf);
  qa_save_reader_field(&f, "no", &vf); level->base.vote.no = qa_save_reader_number(&vf); qa_save_reader_destroy(&vf);
  qa_save_reader_field(&f, "string", &vf); free(level->base.vote.string); level->base.vote.string = dupstr(qa_save_reader_string(&vf)); qa_save_reader_destroy(&vf);
  qa_save_reader_destroy(&f); qa_save_reader_destroy(&reader);
}
