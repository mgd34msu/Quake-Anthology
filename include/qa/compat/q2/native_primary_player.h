/* Port of src/compat/q2/native-primary-player.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_PLAYER_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/source_match.h"

typedef struct qa_compat_q2_native_primary_player_profile {
  qa_content_digest digest; bool has_match; qa_source_primary_match match; double spawn;
  const char *objectives_kind; double objectives_entry; double command_angles, velocity; bool has_forward; double forward;
} qa_compat_q2_native_primary_player_profile;
/* Mirrored guest memory ops (TS owner: guest MappedGuestMemory). */
typedef struct qa_compat_q2_player_memory { void *ctx; int32_t (*read_i32)(void *ctx, uint64_t a); void (*write_i32)(void *ctx, uint64_t a, int32_t v); uint64_t (*client_of)(void *ctx, qa_actor_id a); uint64_t (*entity_of)(void *ctx, qa_actor_id a); } qa_compat_q2_player_memory;
typedef struct qa_compat_q2_native_primary_player { qa_compat_q2_player_memory mem; const qa_compat_q2_native_primary_weapon_profile *weapon; const qa_compat_q2_native_primary_player_profile *profile; } qa_compat_q2_native_primary_player;
void qa_compat_q2_native_primary_player_init(qa_compat_q2_native_primary_player *s, qa_compat_q2_player_memory mem, const char *host_digest, const qa_compat_q2_native_primary_weapon_profile *w, const qa_compat_q2_native_primary_player_profile *p);
double qa_compat_q2_native_primary_player_score(qa_compat_q2_native_primary_player *s, qa_actor_id a);
void qa_compat_q2_native_primary_player_set_score(qa_compat_q2_native_primary_player *s, qa_actor_id a, double score);
double qa_compat_q2_native_primary_player_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id a);
void qa_compat_q2_native_primary_player_set_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id a, double v);
#endif
