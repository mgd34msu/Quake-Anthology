/* Port of src/content/q1/addons/ctf/types.ts. CtfTeam/CtfRune keep
 * exact TS strings ("red"|"blue", NULL = TS null team). Q1CtfServices is
 * the TS host interface (fn + ctx; every member required). GPL-2.0-or-later. */
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
  void (*respawn)(void *ctx, qa_actor_id actor, qa_q1_actor *spot); /* spot NULL = TS null */
  void (*disconnect)(void *ctx, qa_actor_id actor);
  void (*colors)(void *ctx, qa_actor_id actor, double shirt, double pants);
  bool (*prompt_supported)(void *ctx, qa_actor_id actor);
  void (*prompt)(void *ctx, qa_actor_id actor, const char *title,
                 const qa_q1ad_ctf_prompt_choice *choices, size_t count);
  void (*clear_prompt)(void *ctx, qa_actor_id actor);
  void (*teleport)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until);
  void (*select_grapple)(void *ctx, qa_actor_id actor);
  qa_item_id (*selected_weapon)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  qa_item_id (*selected_ammo)(void *ctx, qa_actor_id actor);
  void (*weapon_changed)(void *ctx, qa_actor_id actor, qa_item_id acquired);
  void (*haste)(void *ctx, qa_actor_id actor, bool enabled);
  void (*status)(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status);
  void (*log)(void *ctx, qa_actor_id actor, const char *action);
} qa_q1ad_ctf_services;

#endif
