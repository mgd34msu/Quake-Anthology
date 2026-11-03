/* Port of src/content/q2/foundation/weapons/player.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_PLAYER_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_PLAYER_H
#include "qa/content/q2/foundation/weapons/ballistics.h"

typedef enum qa_q2w_selection {
  QA_Q2W_SEL_SELECTED, QA_Q2W_SEL_CURRENT, QA_Q2W_SEL_NOT_OWNED, QA_Q2W_SEL_NO_AMMO, QA_Q2W_SEL_NOT_ENOUGH_AMMO
} qa_q2w_selection;
const char *qa_q2w_selection_name(qa_q2w_selection selection);

struct qa_q2w_weapons;
typedef struct qa_q2w_context {
  qa_q2w_owner self;
  qa_q2_game_services *game;
  qa_q2w_state *state;
  qa_q2w_input input;
  const qa_q2w_definition *definition;
  double now;
  bool rerelease, silenced;
} qa_q2w_context;

typedef struct qa_q2w_extension {
  const qa_q2w_definition *definition;
  void *ctx;
  void (*fire)(void *ctx, qa_q2w_context *context, struct qa_q2w_weapons *weapons);
  void (*think)(void *ctx, qa_q2w_context *context, struct qa_q2w_weapons *weapons); /* optional */
  void (*held)(void *ctx, qa_q2w_context *context, struct qa_q2w_weapons *weapons, bool held); /* optional */
  qa_q2w_name selection_requested; /* NULL = no selection */
  bool (*selection_choose)(void *ctx, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_state *state);
} qa_q2w_extension;

typedef struct qa_q2w_throw_definition {
  double sound_frame, hold_frame, fire_frame;
  const char *cock_sound, *hold_sound;
  bool explode, wrap_before_pause, release_held;
  void *ctx;
  void (*fire)(void *ctx, qa_q2w_context *context, bool held);
} qa_q2w_throw_definition;

/* Q2WeaponSourceRules: kind "ctf" uses haste/strength/haste_sound;
 * "lmctf" uses post_native_think with a repeat closure (fn+ctx). */
typedef struct qa_q2w_source_rules {
  const char *kind; /* "ctf"|"lmctf" */
  void *ctx;
  bool (*haste)(void *ctx, qa_q2w_context *context);
  bool (*strength_sound)(void *ctx, qa_q2w_context *context);
  void (*haste_sound)(void *ctx, qa_q2w_context *context);
  void (*post_native_think)(void *ctx, qa_q2w_context *context, void *repeat_ctx, void (*repeat)(void *repeat_ctx));
} qa_q2w_source_rules;

/* TS Q2Weapons extends Q2Ballistics. */
typedef struct qa_q2w_dualfire_time { qa_actor_id actor; double time; } qa_q2w_dualfire_time;
typedef struct qa_q2w_weapons {
  qa_q2w_ballistics base;
  /* TS dualfireSoundTime: rerelease Dualfire's ctf/tech3 sound repeats once a second. */
  qa_q2w_dualfire_time *dualfire; size_t dualfire_count, dualfire_cap;
  const qa_q2w_definition **definitions; size_t definition_count, definition_cap;
  qa_q2w_extension *extensions; size_t extension_count, extension_cap;
  qa_q2w_name *fallback_order; size_t fallback_count; bool has_fallback;
  qa_q2w_source_rules source_rules; bool has_source_rules;
} qa_q2w_weapons;

bool qa_q2w_weapons_init(qa_q2w_weapons *w, qa_q2w_hooks hooks);
void qa_q2w_weapons_destroy(qa_q2w_weapons *w);
void qa_q2w_weapons_set_source_rules(qa_q2w_weapons *w, qa_q2w_source_rules rules);
/* capture: out arrays malloc'd; free with qa_q2w_weapons_checkpoint_free. */
bool qa_q2w_weapons_capture(qa_q2w_weapons *w, qa_q2_game_services *game, qa_q2w_weapons_checkpoint *out);
void qa_q2w_weapons_checkpoint_free(qa_q2w_weapons_checkpoint *checkpoint);
void qa_q2w_weapons_restore(qa_q2w_weapons *w, qa_q2_game_services *game, const qa_q2w_weapons_checkpoint *checkpoint);
void qa_q2w_weapons_register(qa_q2w_weapons *w, qa_q2w_extension extension);
const qa_q2w_definition *qa_q2w_weapons_definition(qa_q2w_weapons *w, qa_q2w_name name);
const qa_q2w_definition *qa_q2w_weapons_definition_from_classname(qa_q2w_weapons *w, const char *classname);
const qa_q2w_definition *const *qa_q2w_weapons_registered_definitions(qa_q2w_weapons *w, size_t *out_count);
void qa_q2w_weapons_set_fallback_order(qa_q2w_weapons *w, const qa_q2w_name *order, size_t count);
/* bind: state NULL = new default state; non-NULL state is malloc'd by
 * caller and ownership transfers. */
qa_q2w_state *qa_q2w_weapons_bind(qa_q2w_weapons *w, qa_owned_actor self, qa_q2_game_services *game, qa_q2w_state *state);
qa_q2w_selection qa_q2w_weapons_request_weapon(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_name name, bool allow_empty);
void qa_q2w_weapons_request_holster(qa_q2w_weapons *w, qa_q2w_owner self);
bool qa_q2w_weapons_is_holstered(qa_q2w_weapons *w, qa_q2w_owner self);
bool qa_q2w_weapons_continues_attack(qa_q2w_weapons *w, const qa_q2w_context *context);
void qa_q2w_weapons_resume_primary(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_input input, qa_q2w_name name);
bool qa_q2w_weapons_can_drop(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_name name);
void qa_q2w_weapons_tick(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_input input);
double qa_q2w_weapons_ammo(qa_q2w_weapons *w, qa_q2w_context *context);
/* consume defaults: count = definition.quantity, infinite = true
 * (qa_q2w_weapons_consume_default). */
void qa_q2w_weapons_consume(qa_q2w_weapons *w, qa_q2w_context *context, double count, bool infinite);
void qa_q2w_weapons_consume_default(qa_q2w_weapons *w, qa_q2w_context *context);
void qa_q2w_weapons_no_ammo(qa_q2w_weapons *w, qa_q2w_context *context, bool sound);
/* priority: "attack"|"pain"|"reverse" */
void qa_q2w_weapons_animation(qa_q2w_weapons *w, qa_q2w_context *context, const char *priority, double first, double last);
void qa_q2w_weapons_attack_animation(qa_q2w_weapons *w, qa_q2w_context *context, double offset);
void qa_q2w_weapons_change_weapon(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_state *state, qa_q2w_input input);
double qa_q2w_weapons_firing_interval(qa_q2w_weapons *w, qa_actor_id actor, double seconds);
double qa_q2w_weapons_animation_time(qa_q2w_weapons *w, qa_q2w_context *context);
void qa_q2w_weapons_generic_classic(qa_q2w_weapons *w, qa_q2w_context *context);
void qa_q2w_weapons_generic_rerelease(qa_q2w_weapons *w, qa_q2w_context *context);
double qa_q2w_weapons_multiplier(qa_q2w_weapons *w, qa_q2w_context *context);
void qa_q2w_weapons_powerup_sound(qa_q2w_weapons *w, qa_q2w_context *context);
qa_q2w_projection qa_q2w_weapons_project_source(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_input input, qa_vec3 angles, qa_vec3 offset);
qa_q2w_projection qa_q2w_weapons_project(qa_q2w_weapons *w, qa_q2w_context *context, qa_vec3 offset, qa_vec3 angles);
qa_q2w_projection qa_q2w_weapons_project_default(qa_q2w_weapons *w, qa_q2w_context *context, qa_vec3 offset);
void qa_q2w_weapons_kick(qa_q2w_weapons *w, qa_q2w_context *context, qa_vec3 origin, qa_vec3 angles, double duration);
void qa_q2w_weapons_kick_default(qa_q2w_weapons *w, qa_q2w_context *context, qa_vec3 origin, qa_vec3 angles);
void qa_q2w_weapons_flash(qa_q2w_weapons *w, qa_q2w_context *context, double flash);
void qa_q2w_weapons_set_loop(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_state *state, const char *path);
/* throwing NULL = TS undefined. */
void qa_q2w_weapons_throw_classic(qa_q2w_weapons *w, qa_q2w_context *context, const qa_q2w_throw_definition *throwing);
void qa_q2w_weapons_throw_rerelease(qa_q2w_weapons *w, qa_q2w_context *context, const qa_q2w_throw_definition *throwing);
#endif
