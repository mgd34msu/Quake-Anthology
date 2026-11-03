/*
 * Port of src/content/composition/q1/types.ts (types only), on the one Q1
 * game: Q1Actor is qa_q1_actor, Q1TravelState qa_q1_travel_state,
 * Q1EntityServices qa_q1_services, Q1AddonEvent qa_q1ad_event, CtfStatus
 * qa_q1ad_ctf_status, Q1SourceFinale qa_q1_intermission_result.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_TYPES_H
#define QA_CONTENT_COMPOSITION_Q1_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/equipment.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/ctf/types.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q1/foundation/entity_services.h"

/* TS Q1SourceProgram, in declaration order. */
typedef enum qa_comp_q1_program {
  QA_COMP_Q1_ID1, QA_COMP_Q1_HIPNOTIC, QA_COMP_Q1_ROGUE, QA_COMP_Q1_DOPA,
  QA_COMP_Q1_MG1, QA_COMP_Q1_MG3, QA_COMP_Q1_CTF
} qa_comp_q1_program;
const char *qa_comp_q1_program_name(qa_comp_q1_program program);

typedef struct qa_comp_q1_source_selection {
  qa_comp_q1_program program;
  qa_q1_campaign_binding campaign; /* TS Q1CampaignBinding (base/provider.h) */
  bool has_horde;
  bool horde;
  bool registered;
  bool official_campaign;
} qa_comp_q1_source_selection;

/* One ReadonlyMap<string,string> as an ordered entry list. */
typedef struct qa_comp_userinfo_entry {
  const char *key;
  const char *value;
} qa_comp_userinfo_entry;

typedef struct qa_comp_q1_client_admission {
  int64_t slot;
  const qa_comp_userinfo_entry *userinfo;
  size_t userinfo_count;
} qa_comp_q1_client_admission;

typedef struct qa_comp_q1_source_input {
  bool attack;
  bool jump;
  bool use;
  double impulse;
} qa_comp_q1_source_input;

/* deadFlag is 0|1|2|3; waterType the exact TS string; waterLevel 0..3. */
typedef struct qa_comp_q1_selected_player {
  int dead_flag;
  bool is_bot;
  qa_vec3 view_angles;
  qa_vec3 view_offset;
  double frame;
  const char *water_type;
  int water_level;
  double teleport_until;
} qa_comp_q1_selected_player;

typedef struct qa_comp_q1_client_snapshot {
  qa_actor_id actor;
  int64_t slot;
  const char *name;
  double frags;
  int64_t shirt;
  int64_t pants;
  double team;
  bool observer;
  bool no_target;
  const qa_comp_userinfo_entry *userinfo;
  size_t userinfo_count;
} qa_comp_q1_client_snapshot;

typedef struct qa_comp_q1_prompt_choice {
  const char *label;
  double impulse;
} qa_comp_q1_prompt_choice;

/* TS Q1CompositionEvent union on kind. team is "red" | "blue". */
typedef enum qa_comp_q1_event_kind {
  QA_COMP_Q1_EVENT_ADDON, QA_COMP_Q1_EVENT_CLIENT, QA_COMP_Q1_EVENT_CLIENT_LEFT,
  QA_COMP_Q1_EVENT_CTF_STATUS, QA_COMP_Q1_EVENT_CTF_CAPTURE, QA_COMP_Q1_EVENT_PROMPT,
  QA_COMP_Q1_EVENT_CLEAR_PROMPT, QA_COMP_Q1_EVENT_SOURCE_LOG,
  QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE, QA_COMP_Q1_EVENT_LEVEL_PRESENTATION
} qa_comp_q1_event_kind;
typedef struct qa_comp_q1_event {
  qa_comp_q1_event_kind kind;
  const qa_q1ad_event *addon_event;
  qa_comp_q1_client_snapshot client;
  qa_actor_id actor;
  int64_t slot;
  const qa_q1ad_ctf_status *status;
  const char *team;
  double total;
  const char *title;
  const qa_comp_q1_prompt_choice *choices;
  size_t choice_count;
  const char *action;
  const char *text;
  const qa_q1_intermission_result *finale;
} qa_comp_q1_event;

/** TS Q1CompositionServices as fn+ctx. Optional TS members are NULL-able
 * function pointers (weapon_services, cheat_arsenal, give_selected_item).
 * cheat_arsenal's category is NULL or "weapons" | "ammo". */
typedef struct qa_comp_q1_services {
  void *ctx;
  const qa_shared_grapple_control *shared_grapple; /* NULL when absent */
  double (*cvar)(void *ctx, const char *name);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
  void (*emit)(void *ctx, const qa_comp_q1_event *event);
  qa_comp_q1_selected_player (*selected_player)(void *ctx, qa_actor_id actor);
  void (*set_observer)(void *ctx, qa_actor_id actor, bool enabled);
  void (*place_player)(void *ctx, qa_owned_actor actor, qa_q1_actor *spot,
                       const qa_q1_travel_state *travel);
  void (*disconnect)(void *ctx, qa_actor_id actor);
  void (*teleport)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles,
                   qa_vec3 velocity, double until);
  qa_q1_services *(*weapon_services)(void *ctx, qa_actor_id actor); /* NULL result = TS null */
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count);
  qa_item_id (*selected_weapon)(void *ctx, qa_actor_id actor);
  qa_item_id (*selected_ammo)(void *ctx, qa_actor_id actor);
  bool (*select_weapon)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*weapon_changed)(void *ctx, qa_actor_id actor, qa_item_id acquired);
  bool (*prompt_supported)(void *ctx, qa_actor_id actor);
  void (*restart_session)(void *ctx, const char *map, double starting_server_flags);
  void (*finish_campaign)(void *ctx);
} qa_comp_q1_services;

#endif
