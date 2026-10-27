/* Port of src/content/q2/equipment/grapple-weapon.ts. GPL-2.0-or-later.
 * Mirrored unported types (TS owners: src/content/q2/foundation/weapons/
 * types.ts Q2WeaponDefinition, generic-frame.ts frame state/input):
 * qa_q2f_weapon_definition / qa_q2f_generic_frame_state, collision-proof. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_GRAPPLE_WEAPON_H
#define QA_CONTENT_Q2_EQUIPMENT_GRAPPLE_WEAPON_H
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/content/q2/equipment/lmctf_grapple.h"
typedef struct qa_q2f_weapon_definition {
  const char *name; qa_item_id item; const char *classname; qa_item_id ammo;
  double quantity, warning; const char *view_model, *world_model; double player_model;
  double activate_last, fire_last, idle_last, deactivate_last;
  const double *pauses; size_t pause_count; const double *fires; size_t fire_count; bool repeating;
} qa_q2f_weapon_definition;
extern const qa_q2f_weapon_definition QA_Q2_CTF_GRAPPLE;
extern const qa_q2f_weapon_definition QA_Q2_RERELEASE_CTF_GRAPPLE;
extern const qa_q2f_weapon_definition QA_LMCTF_GRAPPLE;
typedef struct qa_q2f_generic_frame_state {
  const char *phase; double frame; bool latched_attack, source_firing, fire_buffered;
  double think_time, fire_finished, last_firing_time;
} qa_q2f_generic_frame_state;
typedef struct qa_grapple_weapon_input { bool attack, change_requested, holster, latched_holster; } qa_grapple_weapon_input;
typedef struct qa_grapple_step_hooks { void *ctx; void (*reset)(void *ctx); void (*prepare_drop)(void *ctx); void (*generic)(void *ctx); void (*abort)(void *ctx); } qa_grapple_step_hooks;
void qa_step_ctf_grapple_weapon(qa_q2f_generic_frame_state *s, qa_ctf_grapple_state *src, qa_grapple_weapon_input in, qa_q2_edition ed, qa_grapple_step_hooks h);
void qa_step_lmctf_grapple_weapon(qa_q2f_generic_frame_state *s, qa_lmctf_grapple_state *src, qa_grapple_weapon_input in, qa_grapple_step_hooks h);
typedef struct qa_grapple_weapon_presentation {
  void *ctx;
  void (*kick)(void *ctx, qa_vec3 origin, double pitch);
  void (*attack_animation)(void *ctx);
  void (*reverse_animation)(void *ctx);
  void (*powerup_sound)(void *ctx);
  double (*animation_time)(void *ctx, qa_q2f_generic_frame_state *s);
} qa_grapple_weapon_presentation;
void qa_fire_ctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_q2_ctf_grapple_equipment *core, qa_q2f_generic_frame_state *s, qa_q2_edition ed, qa_grapple_weapon_presentation pres);
void qa_fire_lmctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_lmctf_grapple_equipment *core, qa_q2f_generic_frame_state *s, qa_grapple_weapon_presentation pres);
void qa_release_lmctf_grapple_weapon(qa_q2f_generic_frame_state *s);
bool qa_ctf_grapple_weapon_should_reset(qa_q2f_generic_frame_state *s, bool selected, bool change_requested);
/* TS GrappleWeaponSource union on kind: "ctf"(core,edition) | "lmctf"(core). */
typedef struct qa_grapple_weapon_source { const char *kind; qa_q2_ctf_grapple_equipment *ctf; qa_q2_edition edition; qa_lmctf_grapple_equipment *lmctf; } qa_grapple_weapon_source;
typedef struct qa_grapple_weapon_state { qa_q2f_generic_frame_state animation; const char *handoff; } qa_grapple_weapon_state;
qa_grapple_weapon_state qa_create_grapple_weapon_state(void);
/** TS Q2RereleaseFrameInput minus changeRequested, plus latchedHolster. */
typedef struct qa_grapple_weapon_step_input { bool attack, holster, latched_holster, instant_switch, weapon_thunk; double now, frame_seconds; } qa_grapple_weapon_step_input;
typedef struct qa_q2_grapple_weapon {
  qa_actor_id actor; qa_q2_game_services *game; qa_grapple_weapon_source source;
  qa_grapple_weapon_state *state; qa_grapple_weapon_presentation presentation;
} qa_q2_grapple_weapon;
void qa_q2_grapple_weapon_init(qa_q2_grapple_weapon *w, qa_actor_id a, qa_q2_game_services *g, qa_grapple_weapon_source src, qa_grapple_weapon_state *state, qa_grapple_weapon_presentation pres);
const qa_q2f_weapon_definition *qa_q2_grapple_weapon_definition(const qa_q2_grapple_weapon *w);
void qa_q2_grapple_weapon_holster(qa_q2_grapple_weapon *w);
bool qa_q2_grapple_weapon_is_holstered(const qa_q2_grapple_weapon *w);
void qa_q2_grapple_weapon_released(qa_q2_grapple_weapon *w);
void qa_q2_grapple_weapon_resume(qa_q2_grapple_weapon *w);
void qa_q2_grapple_weapon_player_frame(qa_q2_grapple_weapon *w);
void qa_q2_grapple_weapon_step(qa_q2_grapple_weapon *w, qa_grapple_weapon_step_input in);
#endif
