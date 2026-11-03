/* Port of src/app/bootstrap/simulation/network-q2-guest.ts (API 3 guest projection
 * through the shared native Q2 server channel).
 *
 * TS takes `world: ClassicGuestWorld` (and RereleaseGuestWorld in the rerelease
 * adapter). The C guest worlds do not yet expose these members, so the ones the
 * network adapters read are gathered in qa_app_sim_q2_guest_world, one fn+ctx per TS
 * member, over the real contract state types. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"

/* TS entityInfo(slot) fields the adapters read. */
typedef struct qa_app_sim_q2_guest_entity_info {
  double server_flags;
  double areas[2];
  bool has_clusters; const double *clusters; size_t cluster_count; /* has_clusters false is TS null */
  double first_cluster, headnode;
  bool has_owner_slot; double owner_slot;                          /* false is TS null */
} qa_app_sim_q2_guest_entity_info;
/* TS ClassicGuestClient. */
typedef struct qa_app_sim_q2_guest_client { double slot; const char *userinfo; } qa_app_sim_q2_guest_client;
/* TS ClassicGuestMessage / RereleaseGuestMessage (audience union flattened). */
typedef struct qa_app_sim_q2_guest_message {
  bool unicast; double slot;              /* audience.kind "unicast" */
  qa_vec3 origin; const char *scope;      /* "multicast": "all"|"phs"|"pvs" */
  bool reliable;
  const uint8_t *bytes; size_t length;
} qa_app_sim_q2_guest_message;
/* connect() result: userinfo borrowed until the next world call. */
typedef struct qa_app_sim_q2_guest_connection { bool allowed; const char *userinfo; } qa_app_sim_q2_guest_connection;

typedef struct qa_app_sim_q2_guest_world {
  void *ctx;
  /* world.services.options */
  qa_cvar_registry *cvars;
  double max_clients;
  qa_numeric_operations numeric;
  const char *map_path;
  double frame_milliseconds;
  void (*set_configstring)(void *ctx, double index, const char *value); /* world.services.setConfigstring */
  /* configstrings(): ordered Map entries, borrowed until the next world call. */
  const qa_q2_config_string_entry *(*configstrings)(void *ctx, size_t *out_count);
  bool (*is_retired)(void *ctx);
  const qa_app_sim_q2_guest_client *(*clients)(void *ctx, size_t *out_count);
  double (*player_ping)(void *ctx, double slot);
  /* playerState(slot): the classic world fills `classic`, the rerelease world `rerelease`. */
  void (*player_state)(void *ctx, double slot, qa_q2_player_state *classic, qa_q2_rerelease_player_state *rerelease);
  /* entityStates(): the classic world returns qa_q2_entity_state, the rerelease world
   * qa_q2_rerelease_entity_state (borrowed until the next world call). */
  const void *(*entity_states)(void *ctx, size_t *out_count);
  /* entityState(slot), base fields. */
  qa_q2_entity_state (*entity_state)(void *ctx, double slot);
  qa_app_sim_q2_guest_entity_info (*entity_info)(void *ctx, double slot);
  /* connect(slot, userinfo[, socialId, isBot]). */
  qa_app_sim_q2_guest_connection (*connect)(void *ctx, double slot, const char *userinfo, const char *social_id, bool bot);
  void (*begin)(void *ctx, double slot);
  void (*disconnect)(void *ctx, double slot);
  void (*think)(void *ctx, double slot, const qa_user_command *command);
  void (*userinfo)(void *ctx, double slot, const char *value);
  void (*command)(void *ctx, double slot, const char *const *argv, size_t argc, const char *args);
  /* rawMessages(): drains; borrowed until the next world call. */
  const qa_app_sim_q2_guest_message *(*raw_messages)(void *ctx, size_t *out_count);
} qa_app_sim_q2_guest_world;

/* TS createClassicQ2ApplicationServerHost(options & { world }). host.ctx is owned;
 * free with _destroy. */
qa_app_q2_server_host qa_app_sim_create_classic_q2_application_server_host(
    qa_app_sim_q2_server_binding_options options, const qa_app_sim_q2_guest_world *world);
void qa_app_sim_classic_q2_application_server_host_destroy(qa_app_q2_server_host host);
#endif
