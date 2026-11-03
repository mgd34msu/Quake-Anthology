/* Port of src/app/bootstrap/simulation/native-mod-presentation.ts.
 * Each source has its own configstring namespace, decoded by the existing
 * protocol reader. Strings and arrays returned by the presentation are
 * presentation-owned and valid until the next call that rebuilds them.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_MOD_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/mod_client_presentation.h"
#include "qa/contracts/protocol.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/mod_client_presentation.h"
#include "qa/world/session/mods.h"

/* TS NativeModProjection (native-mod-host.ts), used here and by the host. */
typedef struct qa_app_sim_native_mod_projection {
  void *ctx;
  bool (*project)(void *ctx, qa_raw_entity_view record, qa_owned_actor *out);     /* false is TS null */
  bool (*actor_at)(void *ctx, double slot, qa_actor_id *out);                    /* false is TS null */
  bool (*slot_of)(void *ctx, qa_actor_id actor, double *out);                    /* false is TS null */
  bool (*accepts_client)(void *ctx, double slot);
  qa_guest_address (*address)(void *ctx, qa_actor_id actor);
  /* TS importBoundary(name, values, invoke). invoke runs the import with the
   * given values; invoke_ctx is host-owned and outlives the source, so a
   * restore link captured by the provider can replay it after ReadLevel (TS
   * keeps the closure alive the same way). */
  qa_guest_call_result (*import_boundary)(void *ctx, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx,
                                          qa_guest_call_result (*invoke)(void *invoke_ctx, const qa_guest_call_value *values, size_t count));
} qa_app_sim_native_mod_projection;

typedef struct qa_app_sim_native_mod_appearance {
  const char *path; double skin; const char *skin_path; const char *attached_models[3]; size_t attached_model_count;
  double frame, old_frame, effects, render_flags, scale, alpha; bool visible; qa_vec3 origin, angles;
} qa_app_sim_native_mod_appearance;
/* TS Q2PlayerState | Q2RereleasePlayerState. */
typedef struct qa_app_sim_native_mod_player_state { bool rerelease; qa_q2_player_state classic; qa_q2_rerelease_player_state rerelease_state; } qa_app_sim_native_mod_player_state;
typedef struct qa_app_sim_native_mod_clock { double server_frame, time_milliseconds; bool has_frame_time_milliseconds; double frame_time_milliseconds; } qa_app_sim_native_mod_clock;
typedef struct qa_app_sim_native_mod_entity_state { bool active, visible; double sound, event; qa_vec3 origin; double volume, attenuation; } qa_app_sim_native_mod_entity_state;
/* TS NativeModPresentationSource. */
typedef struct qa_app_sim_native_mod_presentation_source {
  const char *edition; /* "classic" | "rerelease" */
  void *ctx;
  const qa_app_sim_configstring *(*configstrings)(void *ctx, size_t *out_count);
  const qa_app_sim_classic_guest_message *(*drain_messages)(void *ctx, size_t *out_count);
  qa_app_sim_native_mod_appearance (*appearance)(void *ctx, double slot);
  const char *(*signature)(void *ctx, double slot);
  qa_app_sim_native_mod_player_state (*player_state)(void *ctx, double slot);
  qa_app_sim_native_mod_clock (*clock)(void *ctx);
  qa_app_sim_native_mod_entity_state (*state)(void *ctx, double slot);
} qa_app_sim_native_mod_presentation_source;

/* TS NativeModPresentationCheckpoint. Lists are malloc'd; layout strings borrow the reader's tree (read) or the presentation (checkpoint). */
typedef struct qa_app_sim_native_mod_fog_entry { qa_saved_actor_id actor; qa_q2_fog_state value; } qa_app_sim_native_mod_fog_entry;
typedef struct qa_app_sim_native_mod_client_entry { qa_saved_actor_id actor; const char *layout; double *inventory; size_t inventory_count; } qa_app_sim_native_mod_client_entry;
typedef struct qa_app_sim_native_mod_presentation_checkpoint {
  qa_app_sim_native_mod_fog_entry *fog; size_t fog_count;
  qa_app_sim_native_mod_client_entry *clients; size_t client_count;
} qa_app_sim_native_mod_presentation_checkpoint;
/* TS readNativeModPresentation(reader). Aborts as the TS throws. */
void qa_app_sim_read_native_mod_presentation(const qa_save_reader *reader, qa_app_sim_native_mod_presentation_checkpoint *out);
void qa_app_sim_native_mod_presentation_checkpoint_free(qa_app_sim_native_mod_presentation_checkpoint *checkpoint);
/* The value tree TS encodes for a checkpoint (NULL on OOM). */
qa_save_value *qa_app_sim_native_mod_presentation_checkpoint_value(const qa_app_sim_native_mod_presentation_checkpoint *checkpoint);

typedef struct qa_app_sim_native_mod_actor_slot { qa_actor_id actor; double slot; } qa_app_sim_native_mod_actor_slot;
typedef struct qa_app_sim_native_mod_presentation qa_app_sim_native_mod_presentation;
/* scene is NativeModHostContext.scene; admission NULL is TS undefined. Borrowed arguments must outlive the presentation. */
qa_app_sim_native_mod_presentation *qa_app_sim_native_mod_presentation_create(qa_app_sim_native_mod_presentation_source source, qa_content_id content,
    const qa_app_sim_native_mod_projection *projection, const qa_mod_host_services *services, qa_shared_scene_queries *scene,
    qa_provider_id owner, const qa_native_mod_client_presentation *admission);
void qa_app_sim_native_mod_presentation_destroy(qa_app_sim_native_mod_presentation *presentation);
double qa_app_sim_native_mod_presentation_generation(const qa_app_sim_native_mod_presentation *presentation);
/* False is the TS null. The frame borrows presentation storage until the next publish. */
bool qa_app_sim_native_mod_presentation_client_frame(qa_app_sim_native_mod_presentation *presentation, qa_actor_id actor, qa_mod_client_presentation_frame *out);
void qa_app_sim_native_mod_presentation_publish(qa_app_sim_native_mod_presentation *presentation, const qa_app_sim_native_mod_actor_slot *actors, size_t count, bool events);
void qa_app_sim_native_mod_presentation_begin_frame(qa_app_sim_native_mod_presentation *presentation);
void qa_app_sim_native_mod_presentation_release(qa_app_sim_native_mod_presentation *presentation, qa_actor_id actor);
void qa_app_sim_native_mod_presentation_close(qa_app_sim_native_mod_presentation *presentation);
void qa_app_sim_native_mod_presentation_drain(qa_app_sim_native_mod_presentation *presentation);
/* Presentation-owned array of SimulationPresentation (opaque = qa_app_simulation_presentation). */
const qa_app_simulation_presentation *qa_app_sim_native_mod_presentation_appearance(qa_app_sim_native_mod_presentation *presentation, qa_actor_id actor, double slot, size_t *out_count);
const char *qa_app_sim_native_mod_presentation_signature(qa_app_sim_native_mod_presentation *presentation, double slot);
/* TS checkpoint(): owned lists; free with qa_app_sim_native_mod_presentation_checkpoint_free. */
void qa_app_sim_native_mod_presentation_capture(qa_app_sim_native_mod_presentation *presentation, qa_app_sim_native_mod_presentation_checkpoint *out);
void qa_app_sim_native_mod_presentation_restore(qa_app_sim_native_mod_presentation *presentation, const qa_app_sim_native_mod_presentation_checkpoint *saved);
#endif
