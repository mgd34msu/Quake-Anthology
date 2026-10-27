/* Port of src/content/q1/addons/ctf/types.ts. CtfTeam/CtfRune keep
 * exact TS strings. Mirrored Threewave grapple/weapon ops stand in for
 * src/content/q1/equipment/threewave-{grapple,weapon}.ts until they
 * land. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_TYPES_H
#define QA_CONTENT_Q1_ADDONS_CTF_TYPES_H
#include "qa/content/q1/addons/context.h"
#include "qa/contracts/equipment.h"

#define QA_Q1AD_CTF_HEALTH_PROTECT 1
#define QA_Q1AD_CTF_ARMOR_PROTECT 2
#define QA_Q1AD_CTF_REFLECT_DAMAGE 4
#define QA_Q1AD_CTF_FRAG_PENALTY 8
#define QA_Q1AD_CTF_DEATH_PENALTY 16
#define QA_Q1AD_CTF_STATIC_TEAMS 64
#define QA_Q1AD_CTF_DROP_ITEMS 128
#define QA_Q1AD_CTF_SELECT_TEAM 1024
#define QA_Q1AD_CTF_DISABLE_GRAPPLE 2048

extern const char *const QA_Q1AD_CTF_RUNES[4];
extern const qa_bounds QA_Q1AD_CTF_FLAG_BOUNDS;

typedef struct qa_q1ad_ctf_input {
  bool attack, jump, grapple_selected;
  double impulse, teleport_until, frame;
  qa_vec3 view_angles;
} qa_q1ad_ctf_input;

typedef struct qa_q1ad_ctf_status {
  double red, blue;
  int64_t flags, rune_items;
} qa_q1ad_ctf_status;

typedef struct qa_q1ad_ctf_prompt_choice {
  const char *label;
  double impulse;
} qa_q1ad_ctf_prompt_choice;

typedef struct qa_q1ad_ctf_services {
  void *ctx;
  const char *(*name)(void *ctx, qa_actor_id actor);
  bool (*is_bot)(void *ctx, qa_actor_id actor);
  double (*score)(void *ctx, qa_actor_id actor);
  void (*add_score)(void *ctx, qa_actor_id actor, double delta);
  double (*captures)(void *ctx, const char *team);
  void (*add_capture)(void *ctx, const char *team);
  qa_q1ad_ctf_input (*input)(void *ctx, qa_actor_id actor);
  void (*consume_impulse)(void *ctx, qa_actor_id actor);
  bool (*observer)(void *ctx, qa_actor_id actor);
  void (*set_observer)(void *ctx, qa_actor_id actor, bool observer);
  void (*respawn)(void *ctx, qa_actor_id actor, qa_q1ad_actor *spot);
  void (*disconnect)(void *ctx, qa_actor_id actor);
  void (*colors)(void *ctx, qa_actor_id actor, double shirt, double pants);
  bool (*prompt_supported)(void *ctx, qa_actor_id actor);
  void (*prompt)(void *ctx, qa_actor_id actor, const char *title,
                 const qa_q1ad_ctf_prompt_choice *choices, size_t count);
  void (*clear_prompt)(void *ctx, qa_actor_id actor);
  void (*teleport)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until);
  void (*select_grapple)(void *ctx, qa_actor_id actor);
  const char *(*selected_weapon)(void *ctx, qa_actor_id actor);
  const char *(*selected_ammo)(void *ctx, qa_actor_id actor);
  void (*weapon_changed)(void *ctx, qa_actor_id actor, const char *acquired);
  void (*haste)(void *ctx, qa_actor_id actor, bool enabled);
  void (*status)(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status);
  void (*log)(void *ctx, qa_actor_id actor, const char *action);
} qa_q1ad_ctf_services;

/* Mirrored ThreewaveGrappleState fields read/written by this slice. */
typedef struct qa_q1ad_grapple_pose {
  double weapon_frame, attack_finished;
  bool axe_pose, has_frame;
  double frame;
} qa_q1ad_grapple_pose;

typedef struct qa_q1ad_threewave_grapple {
  void *ctx;
  bool present;
  bool (*state)(void *ctx, qa_actor_id actor, qa_q1ad_grapple_pose *out);
  void (*set_weapon_frame)(void *ctx, qa_actor_id actor, double frame);
  bool (*pulling)(void *ctx, qa_actor_id actor);
  qa_q1ad_actor *(*hook)(void *ctx, qa_actor_id actor);
  void (*release)(void *ctx, qa_actor_id actor);
  bool (*fire)(void *ctx, qa_actor_id actor);
  void (*touch)(void *ctx, qa_q1ad_actor *hook, qa_actor_id other);
  void (*trail)(void *ctx, qa_actor_id actor);
  bool (*character_pose)(void *ctx, qa_actor_id actor, qa_q1ad_grapple_pose *out);
} qa_q1ad_threewave_grapple;

typedef struct qa_q1ad_threewave_weapon {
  void *ctx;
  bool present;
  bool (*attack)(void *ctx, qa_actor_id actor);
  void (*animate)(void *ctx, qa_actor_id actor);
} qa_q1ad_threewave_weapon;

typedef struct qa_q1ad_ctf_state qa_q1ad_ctf_state;
/* Factories standing in for the unported equipment constructors. */
typedef qa_q1ad_threewave_grapple (*qa_q1ad_grapple_factory)(void *ctx, qa_q1ad_ctf_state *state);
typedef qa_q1ad_threewave_weapon (*qa_q1ad_weapon_factory)(void *ctx, qa_q1ad_ctf_state *state);
#endif
