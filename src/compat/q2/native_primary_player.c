/* Port of src/compat/q2/native-primary-player.ts (score/maxHealth via
 * mirrored memory; spawn/objectives/teleport need guest invoke, see
 * unresolved). GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_player.h"
#include <math.h>
#include <string.h>
void qa_compat_q2_native_primary_player_init(qa_compat_q2_native_primary_player *s, qa_compat_q2_player_memory mem, const char *host_digest, const qa_compat_q2_native_primary_weapon_profile *w, const qa_compat_q2_native_primary_player_profile *p) {
  if (p->has_match && (p->match.score != trunc(p->match.score) || p->match.score < 0 || fmod(p->match.score, 4) != 0 || p->match.score + 4 > w->client_byte_length)) qa_range_error("Native score exceeds the declared client record");
  if (!host_digest || strcmp(host_digest, p->digest) != 0 || strcmp(p->digest, w->digest) != 0) qa_range_error("Native player service belongs to another executable");
  s->mem = mem; s->weapon = w; s->profile = p;
}
double qa_compat_q2_native_primary_player_score(qa_compat_q2_native_primary_player *s, qa_actor_id a) {
  if (!s->profile->has_match) qa_range_error("Original native score storage has no declaration");
  return s->mem.read_i32(s->mem.ctx, s->mem.client_of(s->mem.ctx, a) + (uint64_t)s->profile->match.score);
}
void qa_compat_q2_native_primary_player_set_score(qa_compat_q2_native_primary_player *s, qa_actor_id a, double score) {
  if (!s->profile->has_match) qa_range_error("Original native score storage has no declaration");
  s->mem.write_i32(s->mem.ctx, s->mem.client_of(s->mem.ctx, a) + (uint64_t)s->profile->match.score, (int32_t)qa_source_score(score));
}
double qa_compat_q2_native_primary_player_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id a) {
  return s->mem.read_i32(s->mem.ctx, s->mem.entity_of(s->mem.ctx, a) + (uint64_t)s->weapon->entity_max_health.field.offset);
}
void qa_compat_q2_native_primary_player_set_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id a, double v) {
  if (v != trunc(v) || v <= 0 || v > 2147483647.0) qa_range_error("Native player max health requires a positive int32");
  s->mem.write_i32(s->mem.ctx, s->mem.entity_of(s->mem.ctx, a) + (uint64_t)s->weapon->entity_max_health.field.offset, (int32_t)v);
}
