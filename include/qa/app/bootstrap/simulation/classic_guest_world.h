/* Port of src/app/bootstrap/simulation/classic-guest-world.ts.
 * Travel transfers the retained source and services to a new world
 * object; the C worlds share them through a reference count, so each
 * world (transferred or not) is destroyed by its owner and the guest
 * goes with the last one. Async *Loading variants are synchronous with a
 * next-frame callback. Strings returned by the world (client userinfo,
 * connect results) are world-owned unless noted. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/compat/q2/native_input.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"

typedef struct qa_app_sim_classic_guest_world qa_app_sim_classic_guest_world;
typedef struct qa_app_sim_classic_guest_map { const char *map; const char *entities; const char *spawn_point; } qa_app_sim_classic_guest_map;
typedef struct qa_app_sim_classic_guest_world_options {
  const qa_app_sim_prepared_classic_guest *prepared; /* borrowed for the call */
  qa_app_sim_classic_guest_services_options services;
  qa_windows_capabilities capabilities;
  bool has_instruction_budget; double instruction_budget;
} qa_app_sim_classic_guest_world_options;
typedef struct qa_app_sim_classic_guest_revisit {
  const char *level_path;
  void *ctx; void (*restore_server_state)(void *ctx, qa_app_sim_classic_guest_world *world);
} qa_app_sim_classic_guest_revisit;
/* phase: "connected"|"active". */
typedef struct qa_app_sim_classic_guest_client { double slot; const char *phase; char *userinfo; } qa_app_sim_classic_guest_client;
typedef struct qa_app_sim_classic_guest_connect { bool allowed; const char *userinfo; } qa_app_sim_classic_guest_connect;
typedef struct qa_app_sim_classic_guest_entity_info {
  bool has_actor; qa_actor_id actor; bool active; double server_flags; double areas[2];
  bool clusters_null; double clusters[16]; size_t cluster_count; double first_cluster, headnode;
  bool has_owner_slot; double owner_slot;
} qa_app_sim_classic_guest_entity_info;

qa_app_sim_classic_guest_world *qa_app_sim_classic_guest_world_create(const qa_app_sim_classic_guest_world_options *options);
void qa_app_sim_classic_guest_world_destroy(qa_app_sim_classic_guest_world *world);
bool qa_app_sim_classic_guest_world_is_retired(const qa_app_sim_classic_guest_world *world);
qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_world_source(qa_app_sim_classic_guest_world *world);
qa_app_sim_classic_guest_services *qa_app_sim_classic_guest_world_services(qa_app_sim_classic_guest_world *world);
qa_module_identity qa_app_sim_classic_guest_world_module(qa_app_sim_classic_guest_world *world);
void qa_app_sim_classic_guest_world_bind_input(qa_app_sim_classic_guest_world *world, qa_compat_q2_native_input_services services);
const qa_app_sim_classic_guest_client *qa_app_sim_classic_guest_world_clients(qa_app_sim_classic_guest_world *world, size_t *out_count);
void qa_app_sim_classic_guest_world_init(qa_app_sim_classic_guest_world *world);
void qa_app_sim_classic_guest_world_init_loading(qa_app_sim_classic_guest_world *world, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_classic_guest_world_spawn(qa_app_sim_classic_guest_world *world, const char *map, const char *entities, const char *spawn_point);
void qa_app_sim_classic_guest_world_spawn_loading(qa_app_sim_classic_guest_world *world, const char *map, const char *entities, void (*next_frame)(void *), void *next_ctx, const char *spawn_point);
void qa_app_sim_classic_guest_world_write_travel_level(qa_app_sim_classic_guest_world *world, const char *path);
void qa_app_sim_classic_guest_world_write_travel_level_loading(qa_app_sim_classic_guest_world *world, const char *path, void (*next_frame)(void *), void *next_ctx);
/* Irreversible ownership transfer after the caller has staged the destination map. revisit may be NULL. */
qa_app_sim_classic_guest_world *qa_app_sim_classic_guest_world_travel(qa_app_sim_classic_guest_world *world, qa_app_sim_classic_guest_map map,
    const qa_app_sim_classic_guest_map_services *binding, const qa_app_sim_classic_guest_revisit *revisit);
qa_app_sim_classic_guest_world *qa_app_sim_classic_guest_world_travel_loading(qa_app_sim_classic_guest_world *world, qa_app_sim_classic_guest_map map,
    const qa_app_sim_classic_guest_map_services *binding, void (*next_frame)(void *), void *next_ctx, const qa_app_sim_classic_guest_revisit *revisit);
qa_app_sim_classic_guest_connect qa_app_sim_classic_guest_world_connect(qa_app_sim_classic_guest_world *world, double slot, const char *userinfo);
void qa_app_sim_classic_guest_world_begin(qa_app_sim_classic_guest_world *world, double slot);
qa_app_sim_classic_guest_connect qa_app_sim_classic_guest_world_admit(qa_app_sim_classic_guest_world *world, double slot, const char *userinfo);
void qa_app_sim_classic_guest_world_disconnect(qa_app_sim_classic_guest_world *world, double slot);
void qa_app_sim_classic_guest_world_userinfo(qa_app_sim_classic_guest_world *world, double slot, const char *value);
void qa_app_sim_classic_guest_world_set_userinfo_storage(qa_app_sim_classic_guest_world *world, double slot, const char *value);
void qa_app_sim_classic_guest_world_command(qa_app_sim_classic_guest_world *world, double slot, const char *const *arguments, size_t count, const char *args);
void qa_app_sim_classic_guest_world_server_command(qa_app_sim_classic_guest_world *world, const char *const *arguments, size_t count, const char *args);
/* movement may be NULL (TS undefined). */
void qa_app_sim_classic_guest_world_think(qa_app_sim_classic_guest_world *world, double slot, qa_q2_user_command command, const qa_equipment_movement *movement);
void qa_app_sim_classic_guest_world_frame(qa_app_sim_classic_guest_world *world, double milliseconds);
double qa_app_sim_classic_guest_world_allocate_monster_proxy(qa_app_sim_classic_guest_world *world);
void qa_app_sim_classic_guest_world_bind_monster_proxy(qa_app_sim_classic_guest_world *world, double slot, qa_owned_actor actor);
/* False is the TS null. */
bool qa_app_sim_classic_guest_world_actor(qa_app_sim_classic_guest_world *world, double slot, qa_actor_id *out);
qa_app_sim_classic_guest_entity_info qa_app_sim_classic_guest_world_entity_info(qa_app_sim_classic_guest_world *world, double slot);
/* World-owned array valid until the next call. */
const qa_q2_entity_state *qa_app_sim_classic_guest_world_entity_states(qa_app_sim_classic_guest_world *world, size_t *out_count);
qa_q2_entity_state qa_app_sim_classic_guest_world_entity_state(qa_app_sim_classic_guest_world *world, double slot);
double qa_app_sim_classic_guest_world_player_ping(qa_app_sim_classic_guest_world *world, double slot);
void qa_app_sim_classic_guest_world_set_player_ping(qa_app_sim_classic_guest_world *world, double slot, double ping);
qa_q2_player_state qa_app_sim_classic_guest_world_player_state(qa_app_sim_classic_guest_world *world, double slot);
qa_app_sim_model_appearance qa_app_sim_classic_guest_world_model_appearance(qa_app_sim_classic_guest_world *world, double slot);
const qa_app_sim_classic_guest_message *qa_app_sim_classic_guest_world_raw_messages(qa_app_sim_classic_guest_world *world, size_t *out_count);
const qa_app_sim_configstring *qa_app_sim_classic_guest_world_configstrings(qa_app_sim_classic_guest_world *world, size_t *out_count);
/* Original .sv2 engine metadata is restored before the DLL reads its level file. */
void qa_app_sim_classic_guest_world_restore_configstrings(qa_app_sim_classic_guest_world *world, const qa_app_sim_configstring *values, size_t count);
void qa_app_sim_classic_guest_world_write_original(qa_app_sim_classic_guest_world *world, const char *game_path, const char *level_path, bool autosave);
/* Source save files restore gameplay, not CPU/CRT process state. */
void qa_app_sim_classic_guest_world_restore_original(qa_app_sim_classic_guest_world *world, const char *game_path, const char *level_path,
    qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx);
void qa_app_sim_classic_guest_world_restore_original_loading(qa_app_sim_classic_guest_world *world, const char *game_path, const char *level_path,
    qa_app_sim_classic_guest_map map, void (*restore_server_state)(void *), void *restore_ctx, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_classic_guest_world_close(qa_app_sim_classic_guest_world *world);
void qa_app_sim_classic_guest_world_discard(qa_app_sim_classic_guest_world *world);
/* TS classicGuestUserCommand: the 16-byte usercmd_t. */
void qa_app_sim_classic_guest_user_command(qa_q2_user_command command, uint8_t out[16]);
#endif
