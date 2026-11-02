/*
 * Port of src/compat/qvm/mod-presentation.ts. Async flattened.
 * Body calls are qualified with the landed qa_qvm_qualify_body_calls
 * (TS owner src/compat/qvm/body-scope.ts, body_scope.h).
 * The owned module is built from the supplied module options with a
 * host wrapper reproducing sceneSyscall. Save/restore of host state
 * uses the landed checkpoint value codec.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_PRESENTATION_H
#define QA_COMPAT_QVM_MOD_PRESENTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"
#include "qa/compat/qvm/mod_player_events.h"
#include "qa/compat/qvm/mod_presentation_checkpoint.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/render_record.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/persistence/value.h"

void qa_qvm_validate_mod_presentation(const qa_resolved_qvm_artifact *artifact, qa_module_identity source,
                                      const qa_qvm_mod_presentation_declaration *declaration);

/* TS QvmBodyMesh / QvmBodyPresentation. */
typedef struct qa_qvm_body_mesh {
  qa_qvm_body_part part;
  bool base;
  qa_qvm_source_ref_entity_record *passes; /* owned */
  size_t pass_count;
  size_t pass_capacity;
} qa_qvm_body_mesh;

typedef struct qa_qvm_body_presentation {
  qa_actor_id actor;
  qa_qvm_body_mesh *parts; /* owned */
  size_t part_count;
} qa_qvm_body_presentation;

/* TS QvmPresentationContext. */
typedef struct qa_qvm_presentation_context {
  bool has_client_number;
  int64_t client_number;
  qa_q3_source_game_state_record game_state;
  int64_t game_state_revision;
  int32_t frame_time_milliseconds;
  bool has_time;
  int64_t time_milliseconds;
  qa_vec3 view_origin;
  bool has_view_axis;
  qa_axis view_axis;
  bool has_weapon_presented;
  bool weapon_presented;
  int64_t snapshot_server_time;
  qa_q3_player_state snapshot_player_state;
  double player_slots[64];
  const qa_qvm_scene_context *scene; /* borrowed, nullable */
} qa_qvm_presentation_context;

typedef struct qa_qvm_mod_presentation_options {
  qa_qvm_module_options module_options; /* host is wrapped as in the TS */
  qa_module_identity source;
  const qa_qvm_mod_presentation_declaration *declaration;
  void *ctx;
  void (*context)(void *ctx, qa_qvm_presentation_context *out);
  bool (*actor)(void *ctx, int64_t source_slot, qa_actor_id *out);
  bool (*live)(void *ctx, qa_actor_id actor);
  void (*assert_current)(void *ctx);
} qa_qvm_mod_presentation_options;

typedef struct qa_qvm_mod_presentation {
  qa_qvm_mod_presentation_options options;
  qa_qvm_module module;
  qa_qvm_host_state host_state;
  const char *phase; /* created|initialized|failed|closed */
  bool busy;
  int64_t sequence;
  int64_t revision;
  int64_t frame;
  int64_t hud_frame;
  void *players; /* opaque slot -> actor */
  size_t player_count;
  size_t player_capacity;
  uint8_t *defaults;
  size_t defaults_length;
  const qa_app_q3_source_player_event *active_event;
  size_t removal_ids[4];
  size_t removal_count;
  void *snapshots; /* opaque */
  size_t snapshot_count;
  size_t snapshot_capacity;
  int64_t snapshot_number;
  int64_t scene_revision;
  void *commands; /* opaque */
  size_t command_count;
  size_t command_capacity;
  const char *const *arguments; /* borrowed */
  size_t argument_count;
  void *scene_actors; /* opaque */
  size_t scene_actor_count;
  size_t scene_actor_capacity;
  bool restoring_scene;
  bool has_current_game_state;
  qa_q3_source_game_state_record current_game_state;
  qa_qvm_body_presentation *body_frame;
  size_t body_count;
  size_t body_capacity;
  void *scopes; /* opaque player/mesh scopes */
  qa_qvm_resolve_saved_actor_fn restore_actor;
  void *restore_actor_ctx;
  bool has_command_arguments;
} qa_qvm_mod_presentation;

void qa_qvm_mod_presentation_init(qa_qvm_mod_presentation *presentation,
                                  const qa_qvm_mod_presentation_options *options);
void qa_qvm_mod_presentation_destroy(qa_qvm_mod_presentation *presentation);
const qa_qvm_body_presentation *qa_qvm_mod_presentation_bodies(qa_qvm_mod_presentation *presentation,
                                                               size_t *out_count);
void qa_qvm_mod_presentation_capture_checkpoint(qa_qvm_mod_presentation *presentation, qa_guest_checkpoint *out);
void qa_qvm_mod_presentation_restore_checkpoint(qa_qvm_mod_presentation *presentation, const qa_save_value *value,
                                                qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx);
const char *const *qa_qvm_mod_presentation_arguments(qa_qvm_mod_presentation *presentation, size_t *out_count);
int64_t qa_qvm_mod_presentation_last_sequence(const qa_qvm_mod_presentation *presentation);
void qa_qvm_mod_presentation_initialize(qa_qvm_mod_presentation *presentation, int64_t baseline_sequence);
void qa_qvm_mod_presentation_consume(qa_qvm_mod_presentation *presentation,
                                     const qa_app_q3_source_player_event *event, int64_t sequence);
void qa_qvm_mod_presentation_advance(qa_qvm_mod_presentation *presentation, int64_t frame_sequence);
bool qa_qvm_mod_presentation_console_command(qa_qvm_mod_presentation *presentation,
                                             const char *const *arguments, size_t argument_count);
void qa_qvm_mod_presentation_draw_hud(qa_qvm_mod_presentation *presentation, int64_t frame_sequence);
void qa_qvm_mod_presentation_release(qa_qvm_mod_presentation *presentation, qa_actor_id actor);
void qa_qvm_mod_presentation_close(qa_qvm_mod_presentation *presentation);

#endif
