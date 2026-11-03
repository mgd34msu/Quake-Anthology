/* Port of src/app/bootstrap/simulation/rerelease-guest-world.ts.
 * Travel shares the retained source and services between world objects
 * through a reference count (as classic_guest_world.h). Async *Loading
 * variants are synchronous with a next-frame callback. Write-save results
 * follow qa_compat_q2_rerelease_source_save ownership (native bytes and
 * deferred damage owned by the caller). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"

typedef struct qa_app_sim_rerelease_guest_world qa_app_sim_rerelease_guest_world;
typedef struct qa_app_sim_rerelease_guest_world_options {
  const qa_app_sim_prepared_rerelease_guest *prepared;
  qa_app_sim_rerelease_guest_services_options services;
  qa_windows_capabilities capabilities; /* nowMilliseconds, performanceCounter and performanceFrequency are required */
  bool has_instruction_budget; double instruction_budget;
} qa_app_sim_rerelease_guest_world_options;
typedef struct qa_app_sim_rerelease_guest_save { qa_compat_q2_rerelease_source_save game, level; } qa_app_sim_rerelease_guest_save;
typedef struct qa_app_sim_rerelease_guest_revisit {
  qa_compat_q2_rerelease_source_save level;
  void *ctx; void (*restore_server_state)(void *ctx, qa_app_sim_rerelease_guest_world *world);
} qa_app_sim_rerelease_guest_revisit;

qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_world_create(const qa_app_sim_rerelease_guest_world_options *options);
void qa_app_sim_rerelease_guest_world_destroy(qa_app_sim_rerelease_guest_world *world);
bool qa_app_sim_rerelease_guest_world_is_retired(const qa_app_sim_rerelease_guest_world *world);
qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_world_source(qa_app_sim_rerelease_guest_world *world);
qa_app_sim_rerelease_guest_services *qa_app_sim_rerelease_guest_world_services(qa_app_sim_rerelease_guest_world *world);
qa_module_identity qa_app_sim_rerelease_guest_world_module(qa_app_sim_rerelease_guest_world *world);
const qa_app_sim_classic_guest_client *qa_app_sim_rerelease_guest_world_clients(qa_app_sim_rerelease_guest_world *world, size_t *out_count);
void qa_app_sim_rerelease_guest_world_bind_input(qa_app_sim_rerelease_guest_world *world, qa_compat_q2_native_input_services services);
void qa_app_sim_rerelease_guest_world_init(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_init_loading(qa_app_sim_rerelease_guest_world *world, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_rerelease_guest_world_spawn(qa_app_sim_rerelease_guest_world *world, const char *map, const char *entities, const char *spawn_point);
void qa_app_sim_rerelease_guest_world_spawn_loading(qa_app_sim_rerelease_guest_world *world, const char *map, const char *entities, void (*next_frame)(void *), void *next_ctx, const char *spawn_point);
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_world_travel(qa_app_sim_rerelease_guest_world *world, qa_app_sim_classic_guest_map map,
    const qa_app_sim_rerelease_guest_map_services *binding, const qa_app_sim_rerelease_guest_revisit *revisit);
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_world_travel_loading(qa_app_sim_rerelease_guest_world *world, qa_app_sim_classic_guest_map map,
    const qa_app_sim_rerelease_guest_map_services *binding, void (*next_frame)(void *), void *next_ctx, const qa_app_sim_rerelease_guest_revisit *revisit);
qa_app_sim_classic_guest_connect qa_app_sim_rerelease_guest_world_connect(qa_app_sim_rerelease_guest_world *world, double slot, const char *userinfo, const char *social_id, bool is_bot);
void qa_app_sim_rerelease_guest_world_begin(qa_app_sim_rerelease_guest_world *world, double slot);
qa_app_sim_classic_guest_connect qa_app_sim_rerelease_guest_world_admit(qa_app_sim_rerelease_guest_world *world, double slot, const char *userinfo, const char *social_id, bool is_bot);
void qa_app_sim_rerelease_guest_world_disconnect(qa_app_sim_rerelease_guest_world *world, double slot);
void qa_app_sim_rerelease_guest_world_userinfo(qa_app_sim_rerelease_guest_world *world, double slot, const char *value);
void qa_app_sim_rerelease_guest_world_set_userinfo_storage(qa_app_sim_rerelease_guest_world *world, double slot, const char *value);
void qa_app_sim_rerelease_guest_world_command(qa_app_sim_rerelease_guest_world *world, double slot, const char *const *arguments, size_t count, const char *args);
void qa_app_sim_rerelease_guest_world_server_command(qa_app_sim_rerelease_guest_world *world, const char *const *arguments, size_t count, const char *args);
void qa_app_sim_rerelease_guest_world_think(qa_app_sim_rerelease_guest_world *world, double slot, qa_q2_rerelease_user_command command, const qa_equipment_movement *movement);
void qa_app_sim_rerelease_guest_world_frame(qa_app_sim_rerelease_guest_world *world, double milliseconds);
bool qa_app_sim_rerelease_guest_world_actor(qa_app_sim_rerelease_guest_world *world, double slot, qa_actor_id *out);
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_world_entity_info(qa_app_sim_rerelease_guest_world *world, double slot);
const qa_q2_rerelease_entity_state *qa_app_sim_rerelease_guest_world_entity_states(qa_app_sim_rerelease_guest_world *world, size_t *out_count);
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_world_entity_state(qa_app_sim_rerelease_guest_world *world, double slot);
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_world_player_state(qa_app_sim_rerelease_guest_world *world, double slot);
double qa_app_sim_rerelease_guest_world_player_ping(qa_app_sim_rerelease_guest_world *world, double slot);
void qa_app_sim_rerelease_guest_world_set_player_ping(qa_app_sim_rerelease_guest_world *world, double slot, double ping);
qa_app_sim_model_appearance qa_app_sim_rerelease_guest_world_model_appearance(qa_app_sim_rerelease_guest_world *world, double slot);
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_world_raw_messages(qa_app_sim_rerelease_guest_world *world, size_t *out_count);
const qa_app_sim_configstring *qa_app_sim_rerelease_guest_world_configstrings(qa_app_sim_rerelease_guest_world *world, size_t *out_count);
void qa_app_sim_rerelease_guest_world_restore_configstrings(qa_app_sim_rerelease_guest_world *world, const qa_app_sim_configstring *values, size_t count);
qa_app_sim_rerelease_guest_save qa_app_sim_rerelease_guest_world_write_save(qa_app_sim_rerelease_guest_world *world, bool autosave);
qa_app_sim_rerelease_guest_save qa_app_sim_rerelease_guest_world_write_save_loading(qa_app_sim_rerelease_guest_world *world, bool autosave, void (*next_frame)(void *), void *next_ctx);
qa_compat_q2_rerelease_source_save qa_app_sim_rerelease_guest_world_write_travel_level(qa_app_sim_rerelease_guest_world *world);
qa_compat_q2_rerelease_source_save qa_app_sim_rerelease_guest_world_write_travel_level_loading(qa_app_sim_rerelease_guest_world *world, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_rerelease_guest_world_read_save(qa_app_sim_rerelease_guest_world *world, qa_app_sim_rerelease_guest_save saved, qa_app_sim_classic_guest_map map,
    void (*restore_server_state)(void *), void *restore_ctx);
void qa_app_sim_rerelease_guest_world_read_save_loading(qa_app_sim_rerelease_guest_world *world, qa_app_sim_rerelease_guest_save saved, qa_app_sim_classic_guest_map map,
    void (*restore_server_state)(void *), void *restore_ctx, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_rerelease_guest_world_close(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_discard(qa_app_sim_rerelease_guest_world *world);
/* TS fitRereleaseText(text, capacity) over UTF-8 (malloc'd). */
char *qa_app_sim_fit_rerelease_text(const char *text, size_t capacity);
#endif
